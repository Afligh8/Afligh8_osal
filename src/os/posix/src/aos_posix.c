#include "aos_osal.h"
#include "aos_posix_internal.h"

#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>


static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_is_initialized = false;

int32_t AOS_Init(void)
{
    int32_t ret = AOS_SUCCESS;

    pthread_mutex_lock(&g_init_lock);

    if (!g_is_initialized)
    {   /*
         * Initialize POSIX task support.
         *
         * AOS_PosixTaskInit() is responsible for task-specific
         * setup, including RT scheduler configuration.
         */
        ret = AOS_PosixTaskInit();

        if (ret == AOS_SUCCESS)
        {
            /*
             * Initialize the static POSIX mutex subsystem.
             */
            ret = AOS_PosixMutexInit();
        }

        if (ret == AOS_SUCCESS)
        {
            /*
             * Initialize the static POSIX semaphore subsystem.
             */
            ret = AOS_PosixBinSemInit();
        }

        /*
         * No memory pool subsystem here, deliberately -- this is a
         * DEFERRED decision, not a rejected one.
         *
         * AFlight8's flight-control data is small, strongly typed, and
         * predictable (imu, gps, vehicle_state, attitude_setpoint,
         * actuator_output, ...), including processed-vision results
         * rather than raw camera frames. That data fits a uORB-style
         * topic-owned storage model: each Pub/Sub topic owns its own
         * fixed-size storage (and, where needed, a fixed-depth queue),
         * allocated at startup. A generic mempool/buffer-pool is not a
         * dependency of the OSAL or of the initial Pub/Sub
         * implementation under that model.
         *
         * Revisit only if a concrete subsystem actually needs
         * variable-size or explicit ownership-transfer buffers -- large
         * vision buffers, SD/logging buffers, telemetry/networking
         * packets, DSP buffers, or another (possibly non-flight) project
         * building on this OSAL. If that need materializes, prefer a
         * preallocated fixed-block or size-binned pool with explicit
         * allocate/use/release ownership, predictable failure on
         * exhaustion, and no silent malloc() fallback -- added as its
         * own independent service, not by turning Pub/Sub itself into a
         * generic allocator.
         */
    
        
        /* The whole OSAL is considered initialized only when
         * every required subsystem initialized successfully.
         */

        if (ret == AOS_SUCCESS)
        {
            g_is_initialized = true;
        }
    }

    pthread_mutex_unlock(&g_init_lock);

    return ret;
}

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

    if (!g_is_initialized) {
        return AOS_ERR_GENERIC;
    }

    /*
     * The only bit that is a genuine runtime privilege probe rather
     * than a fixed property of this libc/kernel build -- see
     * AOS_PosixTaskIsRtEnabled()'s doc comment.
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

int32_t AOS_TimeGet(aos_time_t *now_us){

    struct timespec ts;

    if (now_us == NULL){
        return AOS_ERR_INVALID_POINTER;
    }
    
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0){
        return AOS_ERR_GENERIC;
    }
    
    *now_us = (aos_time_t)ts.tv_sec * 1000000 + (aos_time_t)(ts.tv_nsec / 1000);
    return AOS_SUCCESS;
}