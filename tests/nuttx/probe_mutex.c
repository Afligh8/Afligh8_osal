/*
 * Reproducer for a NuttX 13.0.1 kernel bug, kept for the upstream report.
 * No OSAL code involved: raw pthreads only.
 *
 *   A pthread_mutex_timedlock() that TIMES OUT leaves the mutex permanently
 *   unlockable: the holder's unlock returns 0, the next lock times out,
 *   pthread_mutex_destroy() returns EBUSY. Every protocol and type
 *   (PRIO_INHERIT/NONE x NORMAL/ERRORCHECK/RECURSIVE) reproduces it. A
 *   trylock or a wrong-owner unlock before the final lock does not.
 *
 * Run on sim:nsh:   nsh> probe_mutex
 * Expected on a fixed kernel: every "D lock" line reports 0 and every
 * "destroy" reports 0. On 13.0.1 the timed variants report 110/16.
 *
 * The OSAL works around it with AOS_PORT_MUTEX_TIMEDLOCK_POLL in
 * src/os/nuttx/include/aos_port.h.
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t g_m;

static int my_prio(void)
{
    struct sched_param p; int policy = 0;
    pthread_getschedparam(pthread_self(), &policy, &p);
    return p.sched_priority;
}

static void *timed_waiter(void *arg)
{
    struct timespec dl; (void)arg;
    clock_gettime(CLOCK_REALTIME, &dl); dl.tv_nsec += 50 * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    int rc = pthread_mutex_timedlock(&g_m, &dl);
    printf("  A timedlock(50ms) -> %d (%s)  [main prio during wait: n/a]\n", rc, strerror(rc));
    if (rc == 0) pthread_mutex_unlock(&g_m);
    return NULL;
}
static void *try_waiter(void *arg)
{
    (void)arg;
    int rc = pthread_mutex_trylock(&g_m);
    printf("  B trylock -> %d (%s)\n", rc, strerror(rc));
    if (rc == 0) pthread_mutex_unlock(&g_m);
    return NULL;
}
static void *wrong_unlock(void *arg)
{
    (void)arg;
    int rc = pthread_mutex_unlock(&g_m);
    printf("  C unlock-by-non-owner -> %d (%s)\n", rc, strerror(rc));
    return NULL;
}
static void *final_locker(void *arg)
{
    struct timespec dl; (void)arg;
    clock_gettime(CLOCK_REALTIME, &dl); dl.tv_sec += 2;
    int rc = pthread_mutex_timedlock(&g_m, &dl);   /* bounded so a hang shows as ETIMEDOUT */
    printf("  D lock (2 s bound) -> %d (%s)\n", rc, strerror(rc));
    if (rc == 0) pthread_mutex_unlock(&g_m);
    return NULL;
}

static void spawn_join(void *(*fn)(void *), int prio)
{
    pthread_t t; pthread_attr_t a; struct sched_param p = { .sched_priority = prio };
    pthread_attr_init(&a);
    pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&a, SCHED_FIFO);
    pthread_attr_setschedparam(&a, &p);
    pthread_attr_setstacksize(&a, 8192);
    int rc = pthread_create(&t, &a, fn, NULL);
    if (rc != 0) { printf("  pthread_create -> %d\n", rc); return; }
    pthread_join(t, NULL);
}

static void run(const char *label, int with_timed, int with_try, int with_wrong, int protocol, int type)
{
    pthread_mutexattr_t attr; int rc;
    printf("--- %s ---\n", label);
    pthread_mutexattr_init(&attr);
    rc = pthread_mutexattr_setprotocol(&attr, protocol); printf("  setprotocol(%s) -> %d\n", protocol == PTHREAD_PRIO_INHERIT ? "PI" : "NONE", rc);
    rc = pthread_mutexattr_settype(&attr, type);  printf("  settype(%s) -> %d\n", type == PTHREAD_MUTEX_ERRORCHECK ? "ERRORCHECK" : (type == PTHREAD_MUTEX_RECURSIVE ? "RECURSIVE" : "NORMAL"), rc);
    rc = pthread_mutex_init(&g_m, &attr); printf("  init -> %d\n", rc);
    pthread_mutexattr_destroy(&attr);

    printf("  main prio before lock: %d\n", my_prio());
    rc = pthread_mutex_lock(&g_m); printf("  main lock -> %d\n", rc);
    if (with_timed) { spawn_join(timed_waiter, 172); printf("  main prio after A timed out: %d\n", my_prio()); }
    if (with_try)   spawn_join(try_waiter, 172);
    if (with_wrong) spawn_join(wrong_unlock, 172);
    rc = pthread_mutex_unlock(&g_m); printf("  main unlock -> %d, main prio now: %d\n", rc, my_prio());
    spawn_join(final_locker, 172);
    rc = pthread_mutex_destroy(&g_m); printf("  destroy -> %d\n", rc);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    run("timed then D: PI + ERRORCHECK",   1, 0, 0, PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_ERRORCHECK);
    run("timed then D: NONE + ERRORCHECK", 1, 0, 0, PTHREAD_PRIO_NONE,    PTHREAD_MUTEX_ERRORCHECK);
    run("timed then D: PI + NORMAL",       1, 0, 0, PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
    run("timed then D: NONE + NORMAL",     1, 0, 0, PTHREAD_PRIO_NONE,    PTHREAD_MUTEX_NORMAL);
    run("timed then D: PI + RECURSIVE",    1, 0, 0, PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_RECURSIVE);
    return 0;
}
