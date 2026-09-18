#include "aos_osal.h"
#include "aos_pthread_internal.h"

#include <pthread.h>
#include <stdbool.h>

static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_is_initialized = false;

int32_t AOS_Init(void)
{
    int32_t ret = AOS_SUCCESS;

    pthread_mutex_lock(&g_init_lock);

    if (!g_is_initialized)
    {   /*
         * Initialize task support. AOS_PosixTaskInit() is responsible for
         * task-specific setup, including asking the port how scheduling
         * works on this OS (see AOS_PortConfigureScheduling()).
         */
        ret = AOS_PosixTaskInit();

        if (ret == AOS_SUCCESS)
        {
            ret = AOS_PosixMutexInit();
        }

        if (ret == AOS_SUCCESS)
        {
            ret = AOS_PosixBinSemInit();
        }

        /*
         * Mempool: deliberately not initialized here. AFlight8's Pub/Sub
         * uses topic-owned storage instead of a generic pool; see the
         * feat/mempool branch and docs for the deferral rationale.
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

bool AOS_PthreadIsInitialized(void)
{
    return g_is_initialized;
}
