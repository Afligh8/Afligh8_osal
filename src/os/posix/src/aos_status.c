#include "aos_osal.h"

const char *aos_strerror(int32_t code)
{
    switch (code) {
    case AOS_SUCCESS:         return "AOS_SUCCESS";
    case AOS_ERROR:           return "AOS_ERROR";
    case AOS_INVALID_POINTER: return "AOS_INVALID_POINTER";
    default:                  return "AOS_ERR_UNKNOWN";
    }
}