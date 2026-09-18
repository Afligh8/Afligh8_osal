#ifndef AOS_PORT_H
#define AOS_PORT_H

/*
 * Afligh8_osal port: Apache NuttX (13.x), flat build.
 *
 * NuttX is POSIX enough that the shared pthread-family implementation
 * runs unchanged; this port only answers what differs: there is no
 * privilege gate on real-time scheduling, priorities are 1..255 with a
 * kernel work queue that must stay above flight tasks, stacks are small,
 * and capabilities are compile-time facts of the kernel configuration.
 *
 * Kernel configuration this backend REQUIRES (see aos_osal.defconfig next
 * to this file for a fragment to merge into a board's defconfig). Each is
 * enforced below with #error because the API contract depends on it:
 *
 *   CONFIG_PRIORITY_INHERITANCE   AOS mutexes promise priority inheritance
 *   CONFIG_PTHREAD_MUTEX_TYPES    AOS mutexes are ERRORCHECK (double lock and
 *                                 wrong-owner unlock are detected, not UB)
 *   CONFIG_TLS_NCLEANUP >= 1      the task trampoline's cleanup handler marks
 *                                 a cancelled task EXITED; NuttX 13 keeps the
 *                                 cleanup stack in TLS and compiles
 *                                 pthread_cleanup_push() to a no-op when this
 *                                 is 0
 *   CONFIG_CANCELLATION_POINTS    AOS_TaskDelete() uses DEFERRED cancellation;
 *                                 without cancellation points NuttX cancels
 *                                 asynchronously, mid-critical-section
 *   CONFIG_TLS_NELEM >= 1         AOS_TaskGetId() keeps the task id in TLS
 *   CONFIG_DISABLE_PTHREAD unset  obviously
 */

#include <nuttx/config.h>

#include "aos_config.h"   /* AOS_CONFIG_TASK_PRIORITY_LEVELS, for the window check below */

#ifndef CONFIG_PRIORITY_INHERITANCE
#error "Afligh8_osal on NuttX needs CONFIG_PRIORITY_INHERITANCE=y (AOS mutexes promise priority inheritance)"
#endif
#ifndef CONFIG_PTHREAD_MUTEX_TYPES
#error "Afligh8_osal on NuttX needs CONFIG_PTHREAD_MUTEX_TYPES=y (AOS mutexes are ERRORCHECK)"
#endif
#if !defined(CONFIG_TLS_NCLEANUP) || (CONFIG_TLS_NCLEANUP < 1)
#error "Afligh8_osal on NuttX needs CONFIG_TLS_NCLEANUP>=1 (task exit bookkeeping runs in a pthread cleanup handler, which NuttX 13 compiles to a no-op otherwise)"
#endif
#ifndef CONFIG_CANCELLATION_POINTS
#error "Afligh8_osal on NuttX needs CONFIG_CANCELLATION_POINTS=y (AOS_TaskDelete relies on deferred cancellation)"
#endif
#if !defined(CONFIG_TLS_NELEM) || (CONFIG_TLS_NELEM < 1)
#error "Afligh8_osal on NuttX needs CONFIG_TLS_NELEM>=1 (AOS_TaskGetId keeps the id in thread-local storage)"
#endif
#ifdef CONFIG_DISABLE_PTHREAD
#error "Afligh8_osal on NuttX needs pthreads (CONFIG_DISABLE_PTHREAD must not be set)"
#endif

#define AOS_PORT_NAME               "nuttx"

/*
 * The NuttX priority window AOS priorities map onto. AOS 0 (highest) lands
 * on AOS_CONFIG_NUTTX_PRIORITY_HIGHEST, AOS_TASK_PRIORITY_LOWEST on
 * AOS_CONFIG_NUTTX_PRIORITY_LOWEST, linearly in between.
 *
 * PLACEHOLDER VALUES, pending the project's final numbers. The intent they
 * encode: flight tasks sit BELOW the kernel's high-priority work queue
 * (CONFIG_SCHED_HPWORKPRIORITY, 224 by default, where driver interrupt
 * bottom halves run) and ABOVE NSH (SCHED_PRIORITY_DEFAULT, 100) and the
 * low-priority work queue. 32 AOS levels, 3 NuttX levels apart. Override
 * both from the board's Kconfig (CONFIG_AOS_NUTTX_PRIORITY_*) or compile
 * definitions.
 */
#ifndef AOS_CONFIG_NUTTX_PRIORITY_HIGHEST
#define AOS_CONFIG_NUTTX_PRIORITY_HIGHEST 220
#endif
#ifndef AOS_CONFIG_NUTTX_PRIORITY_LOWEST
#define AOS_CONFIG_NUTTX_PRIORITY_LOWEST  127
#endif

#if defined(CONFIG_SCHED_HPWORK) && (AOS_CONFIG_NUTTX_PRIORITY_HIGHEST >= CONFIG_SCHED_HPWORKPRIORITY)
#error "AOS_CONFIG_NUTTX_PRIORITY_HIGHEST must stay below CONFIG_SCHED_HPWORKPRIORITY: flight tasks must not starve the kernel work queue"
#endif
#if (AOS_CONFIG_NUTTX_PRIORITY_HIGHEST - AOS_CONFIG_NUTTX_PRIORITY_LOWEST + 1) < AOS_CONFIG_TASK_PRIORITY_LEVELS
#error "The NuttX priority window is narrower than AOS_CONFIG_TASK_PRIORITY_LEVELS"
#endif

/*
 * Stacks are real memory on an MCU. 2 KiB is enough for the OSAL's own
 * tests; flight tasks size their own. The kernel refuses anything below
 * CONFIG_PTHREAD_STACK_MIN.
 */
#define AOS_PORT_TASK_STACK_DEFAULT 2048u
#define AOS_PORT_TASK_STACK_MIN     ((size_t)CONFIG_PTHREAD_STACK_MIN)

/* NuttX has sem_clockwait(); bounded waits must not move with wall-clock time. */
#define AOS_PORT_SEM_USE_CLOCKWAIT  1

/*
 * NuttX 13.0.1: a pthread_mutex_timedlock() that times out leaves the mutex
 * permanently unlockable (the next lock never succeeds, destroy reports
 * EBUSY), for every protocol and type -- see tests/nuttx/probe_mutex.c for
 * the raw-pthread reproducer. Plain semaphores are unaffected. Until the
 * kernel is fixed, a bounded mutex wait here is a trylock poll: correct,
 * bounded, but without priority inheritance while waiting. AOS_MutexLock
 * (unbounded) still blocks in the kernel with full inheritance.
 */
#define AOS_PORT_MUTEX_TIMEDLOCK_POLL 1
#define AOS_PORT_MUTEX_POLL_US        (CONFIG_USEC_PER_TICK)

#endif /* AOS_PORT_H */
