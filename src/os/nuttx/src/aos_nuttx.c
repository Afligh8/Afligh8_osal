/*
 * NuttX port: capabilities are compile-time facts of the kernel
 * configuration, and scheduling is always real-time within a configured
 * priority window.
 */

#include "aos_osal.h"
#include "aos_config.h"
#include "aos_pthread_internal.h"

#include <nuttx/config.h>

#include <sched.h>
#include <stdio.h>

int32_t AOS_GetCapabilities(uint32_t *caps_out)
{
    uint32_t caps = 0u;

    if (caps_out == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }
    if (!AOS_PthreadIsInitialized()) {
        return AOS_ERR_GENERIC;
    }

    if (AOS_PosixTaskIsRtEnabled()) {
        caps |= AOS_CAP_RT_SCHEDULING;
    }

    /* Guaranteed by the #error guards in aos_port.h, reported anyway so
     * callers keep one code path across ports. */
#ifdef CONFIG_PRIORITY_INHERITANCE
    caps |= AOS_CAP_PRIORITY_INHERIT;
#endif

    /* sem_timedwait / pthread_mutex_timedlock are always built. */
    caps |= AOS_CAP_TIMED_WAIT;

    /* CLOCK_MONOTONIC is unconditional in NuttX 13. */
    caps |= AOS_CAP_MONOTONIC_CLOCK;

    *caps_out = caps;
    return AOS_SUCCESS;
}

int32_t AOS_PortConfigureScheduling(aos_port_sched_t *out)
{
    int sys_min = sched_get_priority_min(SCHED_FIFO);
    int sys_max = sched_get_priority_max(SCHED_FIFO);

    out->rt_enabled   = true;
    out->policy       = SCHED_FIFO;
    out->priority_min = AOS_CONFIG_NUTTX_PRIORITY_LOWEST;
    out->priority_max = AOS_CONFIG_NUTTX_PRIORITY_HIGHEST;

    if (sys_min < 0 || sys_max < 0 ||
        out->priority_min < sys_min || out->priority_max > sys_max ||
        out->priority_min >= out->priority_max) {
        fprintf(stderr,
                "AOS: NuttX priority window %d..%d is outside SCHED_FIFO's %d..%d\n",
                out->priority_min, out->priority_max, sys_min, sys_max);
        return AOS_ERR_UNSUPPORTED;
    }

    return AOS_SUCCESS;
}
