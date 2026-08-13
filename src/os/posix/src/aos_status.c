#include "aos_osal.h"

const char *AOS_StrError(int32_t code)
{
    switch (code){
    case AOS_SUCCESS:              return "AOS_SUCCESS";
    case AOS_ERR_GENERIC:          return "AOS_ERR_GENERIC";
    case AOS_ERR_INVALID_POINTER:  return "AOS_ERR_INVALID_POINTER";
    case AOS_ERR_INVALID_ID:       return "AOS_ERR_INVALID_ID";
    case AOS_ERR_INVALID_SIZE:     return "AOS_ERR_INVALID_SIZE";
    case AOS_ERR_NAME_TOO_LONG:    return "AOS_ERR_NAME_TOO_LONG";
    case AOS_ERR_NAME_TAKEN:       return "AOS_ERR_NAME_TAKEN";
    case AOS_ERR_NO_FREE_IDS:      return "AOS_ERR_NO_FREE_IDS";
    case AOS_ERR_INVALID_PRIORITY: return "AOS_ERR_INVALID_PRIORITY";
    case AOS_ERR_INVALID_PARAM:    return "AOS_ERR_INVALID_PARAM";
    case AOS_ERR_CREATION_FAILED:  return "AOS_ERR_CREATION_FAILED";
    case AOS_ERR_TIMEOUT:          return "AOS_ERR_TIMEOUT";
    case AOS_ERR_BUSY:             return "AOS_ERR_BUSY";
    case AOS_ERR_INVALID_STATE:    return "AOS_ERR_INVALID_STATE";
    default:                       return "AOS_ERR_UNKNOWN";
    }
}