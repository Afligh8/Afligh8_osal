#include "aos_osal.h"
#include "aos_posix_internal.h"

#include <pthread.h>
#include <stdbool.h>
#include <time.h>


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

        // if (ret == AOS_SUCCESS)
        // {
        //     /*
        //      * Initialize the static POSIX memory pool subsystem.
        //      */
        //     ret = AOS_PosixMempoolInit();
        // }
    
        
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