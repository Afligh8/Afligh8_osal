#ifndef AOS_PTHREAD_INTERNAL_H
#define AOS_PTHREAD_INTERNAL_H

/*
 * Internal to the pthread-family backend: the implementation shared by
 * every OS that offers POSIX threads, semaphores, and clocks (Linux,
 * NuttX, ...). Each OS adds a thin port:
 *
 *   src/os/<port>/include/aos_port.h   compile-time knobs (see below)
 *   src/os/<port>/src/aos_<port>.c     AOS_PortConfigureScheduling(),
 *                                      AOS_GetCapabilities()
 *
 * The shared code never includes an OS-specific header directly; anything
 * that differs between ports goes through aos_port.h or the functions
 * declared here.
 *
 * Knobs every aos_port.h must define:
 *   AOS_PORT_NAME                 "posix" | "nuttx" | ...
 *   AOS_PORT_TASK_STACK_DEFAULT   a sensible default task stack, bytes
 *   AOS_PORT_TASK_STACK_MIN       smallest stack the OS accepts, bytes
 *   AOS_PORT_SEM_USE_CLOCKWAIT    1: bounded semaphore waits use
 *                                 sem_clockwait(CLOCK_MONOTONIC), immune
 *                                 to wall-clock steps (a GPS time set);
 *                                 0: sem_timedwait on CLOCK_REALTIME.
 *   AOS_PORT_MUTEX_TIMEDLOCK_POLL 0: AOS_MutexTimedLock is the kernel's
 *                                 pthread_mutex_timedlock (blocks, keeps
 *                                 priority inheritance during the wait);
 *                                 1: it polls pthread_mutex_trylock once
 *                                 per AOS_PORT_MUTEX_POLL_US until the
 *                                 deadline on CLOCK_MONOTONIC. For kernels
 *                                 whose timed lock is unsafe (NuttX 13.0.1
 *                                 corrupts the mutex when the wait times
 *                                 out). No priority inheritance while
 *                                 polling.
 */

#include "aos_osal.h"
#include "aos_port.h"

#include <stdbool.h>

#ifndef AOS_PORT_NAME
#error "aos_port.h must define AOS_PORT_NAME"
#endif
#ifndef AOS_PORT_TASK_STACK_DEFAULT
#error "aos_port.h must define AOS_PORT_TASK_STACK_DEFAULT"
#endif
#ifndef AOS_PORT_TASK_STACK_MIN
#error "aos_port.h must define AOS_PORT_TASK_STACK_MIN"
#endif
#ifndef AOS_PORT_SEM_USE_CLOCKWAIT
#error "aos_port.h must define AOS_PORT_SEM_USE_CLOCKWAIT"
#endif
#ifndef AOS_PORT_MUTEX_TIMEDLOCK_POLL
#error "aos_port.h must define AOS_PORT_MUTEX_TIMEDLOCK_POLL"
#endif
#if AOS_PORT_MUTEX_TIMEDLOCK_POLL && !defined(AOS_PORT_MUTEX_POLL_US)
#error "aos_port.h must define AOS_PORT_MUTEX_POLL_US when AOS_PORT_MUTEX_TIMEDLOCK_POLL is 1"
#endif

int32_t AOS_PosixTaskInit(void);
int32_t AOS_PosixMutexInit(void);
int32_t AOS_PosixBinSemInit(void);

bool AOS_PthreadIsInitialized(void);

/*
 * Whether real-time scheduling was secured at init, i.e. whether
 * AOS task priorities actually order execution. Backs
 * AOS_CAP_RT_SCHEDULING.
 */
bool AOS_PosixTaskIsRtEnabled(void);

/*
 * How task scheduling works on this OS, answered once by the port during
 * AOS_PosixTaskInit():
 *
 *   rt_enabled    : true if `policy` with priorities priority_min..
 *                   priority_max is usable right now. false means every
 *                   task runs under the OS default policy and AOS
 *                   priorities are recorded but do not order execution.
 *   policy        : SCHED_FIFO or SCHED_RR when rt_enabled.
 *   priority_min/ : the native priority window AOS_TASK_PRIORITY_LOWEST
 *   priority_max    and AOS_TASK_PRIORITY_HIGHEST map onto, inclusive.
 *                   Must span at least AOS_CONFIG_TASK_PRIORITY_LEVELS.
 *
 * Linux probes privileges and may legitimately answer rt_enabled=false
 * (or fail under AOS_CONFIG_POSIX_RT_REQUIRED). NuttX always answers
 * true with a configured window that keeps flight tasks below the
 * kernel's high-priority work queue.
 *
 *   returns : AOS_SUCCESS, or AOS_ERR_PERMISSION_DENIED /
 *             AOS_ERR_UNSUPPORTED / AOS_ERR_CREATION_FAILED when the
 *             port treats real-time scheduling as mandatory and cannot
 *             get it.
 */
typedef struct {
    bool rt_enabled;
    int  policy;
    int  priority_min;
    int  priority_max;
} aos_port_sched_t;

int32_t AOS_PortConfigureScheduling(aos_port_sched_t *out);

#endif /* AOS_PTHREAD_INTERNAL_H */
