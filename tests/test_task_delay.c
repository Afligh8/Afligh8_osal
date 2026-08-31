#include "aos_osal.h"
#include "aos_task.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * AOS_TaskDelay() is implemented on clock_nanosleep(CLOCK_MONOTONIC,
 * TIMER_ABSTIME, ...) against a deadline computed once up front, with a
 * manual EINTR retry loop -- clock_nanosleep is explicitly NOT restarted
 * automatically by the kernel regardless of SA_RESTART, so that retry
 * loop is load-bearing, not defensive decoration.
 *
 * A broken implementation -- a relative nanosleep(), or an absolute
 * sleep missing the EINTR retry -- would return after the FIRST signal
 * interruption instead of waiting out the full requested duration. This
 * test proves that doesn't happen, by genuinely interrupting a blocked
 * AOS_TaskDelay() call with real POSIX signals from a second thread and
 * measuring how long it actually waited.
 */

#define DELAY_TARGET_MS   200u
#define INTERRUPT_PERIOD_MS 20u

static volatile sig_atomic_t g_signal_count = 0;

static void SigNoop(int signo)
{
    (void)signo;
    g_signal_count++;
}

/*
 * Published by the AOS task under test once it has a valid pthread_t to
 * be interrupted at -- test-only scaffolding, not part of the AOS
 * public API (AOS deliberately never exposes the underlying pthread_t).
 */
static volatile bool g_target_ready = false;
static pthread_t g_target_thread;

static volatile bool g_stop_interrupter = false;

static void *interrupter_thread(void *arg)
{
    (void)arg;

    while (!g_stop_interrupter)
    {
        struct timespec poke_interval;

        poke_interval.tv_sec = 0;
        poke_interval.tv_nsec = (long)INTERRUPT_PERIOD_MS * 1000000L;

        (void)nanosleep(&poke_interval, NULL);

        pthread_kill(g_target_thread, SIGUSR1);
    }

    return NULL;
}

static volatile bool g_worker_done = false;
static volatile int32_t g_worker_status = AOS_ERR_GENERIC;
static aos_time_t g_worker_elapsed_us = 0;

static void delayed_task(void *arg)
{
    struct sigaction sa;
    aos_time_t start_us = 0;
    aos_time_t end_us = 0;

    (void)arg;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SigNoop;
    sigemptyset(&sa.sa_mask);
    /*
     * Deliberately no SA_RESTART: the point is to interrupt the sleep,
     * not have the kernel silently paper over it.
     */
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    g_target_thread = pthread_self();
    g_target_ready = true;

    (void)AOS_TimeGet(&start_us);

    g_worker_status = AOS_TaskDelay(DELAY_TARGET_MS);

    (void)AOS_TimeGet(&end_us);

    g_worker_elapsed_us = end_us - start_us;
    g_worker_done = true;
}

int main(void)
{
    aos_task_t task_id = AOS_TASK_NONE;
    pthread_t interrupter;
    unsigned int i;
    long elapsed_ms;

    if (AOS_Init() != AOS_SUCCESS)
    {
        fprintf(stderr, "AOS_Init failed\n");
        return 1;
    }

    if (AOS_TaskCreate(
            &task_id,
            "delay-eintr",
            delayed_task,
            NULL,
            NULL,
            64u * 1024u,
            AOS_TASK_PRIORITY_DEFAULT,
            AOS_TASK_FLAG_NONE) != AOS_SUCCESS)
    {
        return 2;
    }

    for (i = 0u; i < 500u && !g_target_ready; ++i)
    {
        (void)AOS_TaskDelay(1u);
    }

    if (!g_target_ready)
    {
        fprintf(stderr, "Worker never published a target thread\n");
        return 3;
    }

    if (pthread_create(&interrupter, NULL, interrupter_thread, NULL) != 0)
    {
        fprintf(stderr, "Failed to start interrupter thread\n");
        return 4;
    }

    for (i = 0u; i < 500u && !g_worker_done; ++i)
    {
        (void)AOS_TaskDelay(10u);
    }

    g_stop_interrupter = true;
    pthread_join(interrupter, NULL);

    if (!g_worker_done)
    {
        fprintf(stderr, "Worker did not complete\n");
        return 5;
    }

    if (AOS_TaskJoin(task_id) != AOS_SUCCESS)
    {
        return 6;
    }

    if (AOS_TaskDelete(task_id) != AOS_SUCCESS)
    {
        return 7;
    }

    if (g_signal_count < 3)
    {
        fprintf(stderr,
                "Interrupter only fired %d times -- test setup problem, "
                "not evidence about AOS_TaskDelay\n",
                (int)g_signal_count);
        return 8;
    }

    if (g_worker_status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "AOS_TaskDelay returned %s under signal interruption, "
                "expected AOS_SUCCESS\n",
                AOS_StrError(g_worker_status));
        return 9;
    }

    elapsed_ms = (long)(g_worker_elapsed_us / 1000);

    printf("Requested %ums delay, interrupted %d times by real signals, "
           "measured %ldms elapsed\n",
           DELAY_TARGET_MS, (int)g_signal_count, elapsed_ms);

    /*
     * Must not return early. A broken implementation -- missing EINTR
     * retry, or a relative sleep that doesn't account for time already
     * elapsed -- would return around the FIRST interrupt, i.e. roughly
     * INTERRUPT_PERIOD_MS (~20ms), nowhere near the full 200ms request.
     */
    if (elapsed_ms < (long)DELAY_TARGET_MS - 10)
    {
        fprintf(stderr,
                "AOS_TaskDelay returned early under signal interruption: "
                "requested %ums, only waited %ldms\n",
                DELAY_TARGET_MS, elapsed_ms);
        return 10;
    }

    /*
     * Generous upper bound: catches a gross regression (e.g. deadline
     * recomputed from "now" on every retry instead of held fixed,
     * compounding into an ever-growing wait) without being flaky on a
     * loaded or sandboxed scheduler.
     */
    if (elapsed_ms > (long)DELAY_TARGET_MS + 500)
    {
        fprintf(stderr,
                "AOS_TaskDelay waited far longer than requested: "
                "requested %ums, waited %ldms\n",
                DELAY_TARGET_MS, elapsed_ms);
        return 11;
    }

    puts("ALL TASK DELAY TESTS PASSED");

    return 0;
}
