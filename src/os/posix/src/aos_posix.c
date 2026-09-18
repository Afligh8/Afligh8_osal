/*
 * Linux port: capabilities via POSIX option probing, and the real-time
 * scheduling privilege probe.
 */

#include "aos_osal.h"
#include "aos_config.h"
#include "aos_pthread_internal.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * A POSIX "option" macro (from <unistd.h>) is one of:
 *   > 0  : always supported by this build -- no runtime check needed.
 *   == 0 : "maybe" -- this libc/kernel combination could go either way,
 *          the standard requires asking sysconf() at runtime.
 *   < 0  : never supported.
 *
 * _POSIX_MONOTONIC_CLOCK is a real example of the "== 0" case on this
 * exact glibc: defined as 0 at compile time, yet sysconf(_SC_MONOTONIC_
 * CLOCK) reports it supported at runtime on a normal Linux kernel. Only
 * the option/sysconf pairing below can answer that correctly; a bare
 * #ifdef would not.
 */
static bool AOS_PosixOptionSupported(long option, int sysconf_name)
{
    if (option > 0) {
        return true;
    }
    if (option < 0) {
        return false;
    }
    return sysconf(sysconf_name) > 0;
}

int32_t AOS_GetCapabilities(uint32_t *caps_out)
{
    uint32_t caps = 0u;

    if (caps_out == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (!AOS_PthreadIsInitialized()) {
        return AOS_ERR_GENERIC;
    }

    /*
     * The only bit that is a genuine runtime privilege probe rather
     * than a fixed property of this libc/kernel build -- see
     * AOS_PortConfigureScheduling() below.
     */
    if (AOS_PosixTaskIsRtEnabled()) {
        caps |= AOS_CAP_RT_SCHEDULING;
    }

    if (AOS_PosixOptionSupported(_POSIX_THREAD_PRIO_INHERIT, _SC_THREAD_PRIO_INHERIT)) {
        caps |= AOS_CAP_PRIORITY_INHERIT;
    }

    if (AOS_PosixOptionSupported(_POSIX_TIMEOUTS, _SC_TIMEOUTS)) {
        caps |= AOS_CAP_TIMED_WAIT;
    }

    if (AOS_PosixOptionSupported(_POSIX_MONOTONIC_CLOCK, _SC_MONOTONIC_CLOCK)) {
        caps |= AOS_CAP_MONOTONIC_CLOCK;
    }

    *caps_out = caps;

    return AOS_SUCCESS;
}

static bool AOS_PosixPolicyHasEnoughLevels(int policy, int *min_priority, int *max_priority)
{
    int min = sched_get_priority_min(policy);
    int max = sched_get_priority_max(policy);

    if (min < 0 || max < 0) {
        return false;
    }
    if (min >= max) {
        return false;
    }

    if ((unsigned)(max - min + 1) < AOS_CONFIG_TASK_PRIORITY_LEVELS) {
        return false;
    }

    if (min_priority) {
        *min_priority = min;
    }
    if (max_priority) {
        *max_priority = max;
    }
    return true;
}

/*
 * Linux: real-time scheduling needs a privilege (CAP_SYS_NICE or an
 * rtprio ulimit) that this process may or may not have. Probe it once by
 * briefly switching the calling thread to the RT policy and back.
 */
int32_t AOS_PortConfigureScheduling(aos_port_sched_t *out)
{
    int fifo_min = 0;
    int fifo_max = 0;
    int rr_min = 0;
    int rr_max = 0;
    bool fifo_ok;
    bool rr_ok;
    int original_policy;
    struct sched_param original_param;
    struct sched_param probe_param;
    int rc;

    out->rt_enabled   = false;
    out->policy       = SCHED_OTHER;
    out->priority_min = 0;
    out->priority_max = 0;

    fifo_ok = AOS_PosixPolicyHasEnoughLevels(SCHED_FIFO, &fifo_min, &fifo_max);
    rr_ok   = AOS_PosixPolicyHasEnoughLevels(SCHED_RR, &rr_min, &rr_max);

    if (fifo_ok) {
        out->policy = SCHED_FIFO;
        out->priority_min = fifo_min;
        out->priority_max = fifo_max;
    } else if (rr_ok) {
        out->policy = SCHED_RR;
        out->priority_min = rr_min;
        out->priority_max = rr_max;
    } else {
#if AOS_CONFIG_POSIX_RT_REQUIRED
        /*
         * Not a privilege problem: neither SCHED_FIFO nor SCHED_RR on
         * this platform offers enough distinct priority levels to map
         * AOS_CONFIG_TASK_PRIORITY_LEVELS onto. No process could ever
         * satisfy this by acquiring more permissions.
         */
        return AOS_ERR_UNSUPPORTED;
#else
        return AOS_SUCCESS;
#endif
    }

    rc = pthread_getschedparam(pthread_self(), &original_policy, &original_param);
    if (rc != 0) {
#if AOS_CONFIG_POSIX_RT_REQUIRED
        return AOS_ERR_CREATION_FAILED;
#else
        out->rt_enabled = false;
        return AOS_SUCCESS;
#endif
    }

    probe_param.sched_priority = out->priority_min;
    rc = pthread_setschedparam(pthread_self(), out->policy, &probe_param);
    if (rc == 0) {
        /*
         * The RT scheduling probe succeeded. We only changed the current
         * thread temporarily to verify that this process has permission
         * to use the selected real-time scheduling policy; restore it.
         * Actual AOS tasks receive their policy and priority when
         * AOS_TaskCreate() is called.
         */
        out->rt_enabled = true;

        rc = pthread_setschedparam(pthread_self(), original_policy, &original_param);
        if (rc != 0) {
            fprintf(stderr,
                    "AOS: failed to restore original scheduler: %s\n",
                    strerror(rc));
            return AOS_ERR_CREATION_FAILED;
        }

        return AOS_SUCCESS;
    }

#if AOS_CONFIG_POSIX_RT_REQUIRED
    /*
     * rc is the pthread-style error number straight from
     * pthread_setschedparam() (not errno) -- EPERM means the process
     * lacks the privilege (e.g. CAP_SYS_NICE / an rtprio ulimit) to use
     * this scheduling policy, which is exactly what AOS_ERR_PERMISSION_
     * DENIED exists to name precisely instead of folding it into a
     * generic creation failure. EINVAL means the platform rejected the
     * policy/priority combination outright, i.e. it doesn't support it.
     */
    if (rc == EPERM) {
        return AOS_ERR_PERMISSION_DENIED;
    }
    if (rc == EINVAL) {
        return AOS_ERR_UNSUPPORTED;
    }
    return AOS_ERR_CREATION_FAILED;
#else
    out->rt_enabled = false;
#if AOS_CONFIG_POSIX_RT_WARN_FALLBACK
    fprintf(stderr,
            "AOS: POSIX real-time scheduling unavailable (%s); falling back to SCHED_OTHER\n",
            strerror(rc));
#endif
    return AOS_SUCCESS;
#endif
}
