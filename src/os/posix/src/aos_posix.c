/*
 * aos_posix.c — POSIX backend for AOS OSAL.
 * Portable to any POSIX OS (Linux now, NuttX later). Grows one module at a time.
 *
 * The feature-test macro below is required under -std=c99: it tells glibc to
 * expose the POSIX.1-2008 functions (clock_gettime, CLOCK_MONOTONIC). Without it,
 * strict C99 hides them and you get implicit-declaration warnings/errors.
*/


#include "aos_osal.h"
#include <time.h>

int32_t AOS_Init(void){

    return AOS_SUCCESS;
}

int32_t AOS_TimeGet(aos_time_t *now_us){

    struct timespec ts;

    if (now_us == NULL)
    {
        return AOS_INVALID_POINTER;
    }
    
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return AOS_ERROR;
    }
    
    *now_us = (aos_time_t)ts.tv_sec * 1000000 + (aos_time_t)(ts.tv_nsec / 1000);
    return AOS_SUCCESS;
}