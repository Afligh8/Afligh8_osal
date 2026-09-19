#include "aos_osal.h"

#include <time.h>

int32_t AOS_TimeGet(aos_time_t *now_us)
{
    struct timespec ts;

    if (now_us == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return AOS_ERR_GENERIC;
    }

    *now_us = (aos_time_t)ts.tv_sec * 1000000 + (aos_time_t)(ts.tv_nsec / 1000);
    return AOS_SUCCESS;
}
