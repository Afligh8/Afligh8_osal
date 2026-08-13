#include "aos_osal.h"
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

/* Private POSIX task-module initializer implemented in aos_task.c. */
int32_t AOS_PosixTaskInit(void);

static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_is_initialized = false;

int32_t AOS_Init(void)
{
    int32_t ret = AOS_SUCCESS;

    pthread_mutex_lock(&g_init_lock);
    if (!g_is_initialized)
    {
        ret = AOS_PosixTaskInit();
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