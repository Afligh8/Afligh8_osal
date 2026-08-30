#define _GNU_SOURCE

#include "aos_osal.h"
#include "aos_task.h"
#include "aos_mutex.h"

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <time.h>

// convention to remember 0 is highest 31 is lowest priority, so 4 is a high priority, 16 is medium, and 24 is low
#define PI_PRIORITY_HIGH  ((aos_task_priority_t)(4u))
#define PI_PRIORITY_MEDIUM ((aos_task_priority_t)(16u))
#define PI_PRIORITY_LOW ((aos_task_priority_t)(24u))

static aos_mutex_t g_pi_mutex = AOS_MUTEX_NONE;


/*
 * Four participants:
 *
 * LOW
 * MEDIUM
 * HIGH
 * main()
 */
static pthread_barrier_t g_start_barrier;

/*
 * LOW tells main when it definitely owns
 * the mutex.
 */
static sem_t g_low_locked;

/*
 * Every worker posts this on completion.
 */
static sem_t g_done;

static int g_test_cpu = -1;

/*
 * Scheduler information recorded from the
 * created AOS threads.
 */
static int g_low = SCHED_OTHER;
static int g_medium = SCHED_OTHER;
static int g_high = SCHED_OTHER;

static int g_low_priority = 0;
static int g_medium_priority = 0;
static int g_high_priority = 0;

/*
 * How long HIGH had to wait for the mutex.
 */
static double g_high_wait_time = 0.0;

static double monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return -1.0;
    }

    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
}

//the cpu work
static void busy_work(double duration_ms){
    double end_time = monotonic_ms() + duration_ms;

    while (monotonic_ms() < end_time)
    {
        /*
         * Deliberately stay RUNNABLE.
         *
         * No sleep.
         * No AOS_TaskDelay().
         *
         * We want genuine CPU competition.
         */
    }
}

static int select_test_cpu(void){
    cpu_set_t allowed;

    CPU_ZERO(&allowed);

    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0)
    {
        return -1;
    }

    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
    {
        if (CPU_ISSET(cpu, &allowed))
        {
            return cpu;
        }
    }

    return -1;
}

//Pin each test task
static int pin_current_task(void)
{
    cpu_set_t set;

    CPU_ZERO(&set);

    CPU_SET(g_test_cpu,&set);

    return pthread_setaffinity_np(pthread_self(), sizeof(set),&set);
}

static void get_current_scheduler(int *policy_out, int *priority_out){

    struct sched_param param;
    
    int policy;

    if (pthread_getschedparam(pthread_self(), &policy, &param) != 0){
        *policy_out = SCHED_OTHER;
        *priority_out = 0;
        return;
    }
    
    *policy_out = policy;
    *priority_out = param.sched_priority;
}

//LOW must own the mutex before HIGH even gets a chance to request it.

static void low_task(void *arg)
{
    (void)arg;
    if (pin_current_task() != 0){
        sem_post(&g_done);
        return;
    }

    get_current_scheduler(&g_low, &g_low_priority);

    /*
     * LOW acquires the shared mutex first.
     */
    if (AOS_MutexLock(g_pi_mutex) != AOS_SUCCESS){
        sem_post(&g_done);
        return;
    }

    /*
     * Tell main:
     *
     * LOW definitely owns the mutex now.
     */
    sem_post(&g_low_locked);

    /*
     * Wait until all participants are ready.
     *
     * LOW still owns the mutex while waiting
     * here.
     */
    pthread_barrier_wait(&g_start_barrier);

    /*
     * Critical-section CPU work.
     */
    busy_work(40.0);

    (void)AOS_MutexUnlock(g_pi_mutex);

    sem_post(&g_done);
}

static void medium_task(void *arg)
{
    (void)arg;

    if (pin_current_task() != 0){
        sem_post(&g_done);
        return;
    }
    
    get_current_scheduler(&g_medium, &g_medium_priority);

    pthread_barrier_wait(&g_start_barrier);

    /*
     * MEDIUM deliberately occupies the CPU
     * much longer than LOW needs.
     */
    busy_work(300.0);

    sem_post(&g_done);
}

static void high_task(void *arg)
{
    double start_time;
    double end_time;

    (void)arg;

    if (pin_current_task() != 0){
        sem_post(&g_done);
        return;
    }

    get_current_scheduler(&g_high, &g_high_priority);

    pthread_barrier_wait(&g_start_barrier);

    /*
     * HIGH should run first after the barrier
     * because it has the highest RT priority.
     */
    start_time = monotonic_ms();


    if (AOS_MutexLock(g_pi_mutex) == AOS_SUCCESS)
    {
        end_time = monotonic_ms();

        g_high_wait_time = end_time - start_time;

        (void)AOS_MutexUnlock(
            g_pi_mutex);
    }

    sem_post(&g_done);
}

int main(void)
{
    aos_task_t low_id = AOS_TASK_NONE;

    aos_task_t medium_id = AOS_TASK_NONE;

    aos_task_t high_id = AOS_TASK_NONE;

    int32_t status;

    status = AOS_Init();

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
            "AOS_Init failed: %s\n",
            AOS_StrError(status));

        return 1;
    }

    g_test_cpu =
        select_test_cpu();

    if (g_test_cpu < 0){
        fprintf(stderr, "No usable CPU found\n");

        return 2;
    }


    printf(
        "Using CPU %d for PI test\n", g_test_cpu);


    status = AOS_MutexCreate(&g_pi_mutex, "pi-mutex", AOS_MUTEX_FLAG_NONE);


    if (status != AOS_SUCCESS)
    {
        fprintf(stderr, "Mutex creation failed: %s\n", AOS_StrError(status));

        return 3;
    }

    if (pthread_barrier_init(&g_start_barrier, NULL, 4) != 0){
        return 4;
    }

    if (sem_init(&g_low_locked, 0, 0) != 0){
        return 5;
    }

    if (sem_init(&g_done, 0, 0) != 0){
        return 6;
    }


    /*
     * Create LOW first.
     */
    status = AOS_TaskCreate(
        &low_id,
        "pi-low",
        low_task,
        NULL,
        NULL,
        64u * 1024u,
        PI_PRIORITY_LOW,
        AOS_TASK_FLAG_NONE);


    if (status != AOS_SUCCESS){
        return 7;
    }

    /*
     * Critical synchronization point:
     *
     * Don't create the competing tasks until
     * LOW definitely owns the mutex.
     */
    if (sem_wait(&g_low_locked) != 0){
        return 8;
    }

    printf("LOW owns mutex\n");

    /*
     * MEDIUM CPU competitor.
     */
    status = AOS_TaskCreate(
        &medium_id,
        "pi-medium",
        medium_task,
        NULL,
        NULL,
        64u * 1024u,
        PI_PRIORITY_MEDIUM,
        AOS_TASK_FLAG_NONE);


    if (status != AOS_SUCCESS){
        return 9;
    }

    /*
     * HIGH mutex waiter.
     */
    status = AOS_TaskCreate(
        &high_id,
        "pi-high",
        high_task,
        NULL,
        NULL,
        64u * 1024u,
        PI_PRIORITY_HIGH,
        AOS_TASK_FLAG_NONE);

    if (status != AOS_SUCCESS){
        return 10;
    }

    /*
     * LOW + MEDIUM + HIGH are all waiting
     * at the barrier.
     *
     * Main is participant #4.
     */
    pthread_barrier_wait(
        &g_start_barrier);

    /*
     * Wait for all three workers.
     */
    sem_wait(&g_done);
    sem_wait(&g_done);
    sem_wait(&g_done);

    printf("LOW:    policy=%d native_priority=%d\n",
            g_low,
            g_low_priority);

    printf("MEDIUM: policy=%d native_priority=%d\n",
            g_medium,
            g_medium_priority);

    printf("HIGH:   policy=%d native_priority=%d\n",
            g_high,
            g_high_priority);


    printf("HIGH mutex wait time: %.2f ms\n",
            g_high_wait_time);

    /*
     * Normal developer run:
     *
     * SCHED_OTHER fallback means this test cannot
     * prove RT priority inheritance.
     */
    if (g_high != SCHED_FIFO &&
        g_high != SCHED_RR){
        printf("PRIORITY INHERITANCE TEST SKIPPED: "
                "RT scheduler not active\n");

        (void)AOS_TaskDelete(low_id);
        (void)AOS_TaskDelete(medium_id);
        (void)AOS_TaskDelete(high_id);

        (void)AOS_MutexDelete(g_pi_mutex);

        pthread_barrier_destroy(&g_start_barrier);

        sem_destroy(&g_low_locked);
        sem_destroy(&g_done);

        /*
         * Special skip return code.
         */
        return 77;
    }

    /*
     * LOW performs about 40 ms of work.
     *
     * MEDIUM performs about 300 ms.
     *
     * With PI working, HIGH should wait close
     * to LOW's critical-section duration rather
     * than MEDIUM's 300 ms workload.
     *
     * Keep threshold deliberately generous on
     * a desktop Linux machine.
     */
    if (g_high_wait_time > 150.0)
    {
        fprintf(stderr,
                "PRIORITY INHERITANCE TEST FAILED: "
                "HIGH waited %.2f ms\n",
                g_high_wait_time);

        return 11;
    }

    printf("PRIORITY INHERITANCE TEST PASSED: "
            "HIGH waited %.2f ms\n",
            g_high_wait_time);

    if (AOS_TaskDelete(low_id) != AOS_SUCCESS){
        return 12;
    }

    if (AOS_TaskDelete(medium_id) != AOS_SUCCESS){
        return 13;
    }

    if (AOS_TaskDelete(high_id) != AOS_SUCCESS){
        return 14;
    }

    if (AOS_MutexDelete(g_pi_mutex) != AOS_SUCCESS){
        return 15;
    }

    pthread_barrier_destroy(&g_start_barrier);

    sem_destroy(&g_low_locked);

    sem_destroy(&g_done);

    puts("ALL PRIORITY INHERITANCE TESTS PASSED");

    return 0;
}

