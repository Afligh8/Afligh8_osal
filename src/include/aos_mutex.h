#ifndef AOS_MUTEX_H
#define AOS_MUTEX_H

#include "aos_osal.h"

#ifdef __cplusplus
extern "C" {
#endif 

/*
 * Mutex creation flags.
 *
 * No optional behaviour is currently defined, but the flags field is
 * retained so the API can grow without changing AOS_MutexCreate().
 */
#define AOS_MUTEX_FLAG_NONE 0u

/*
 * Information available for an AOS mutex.
 *
 * creator contains the AOS task ID which created the mutex when creation
 * occurred from an AOS-managed task.
 *
 * If the mutex was created from the application's main thread or another
 * non-AOS thread, creator is AOS_ID_NONE.
 */
typedef struct
{
    char  name[AOS_MAX_NAME];
    aos_task_t creator;
} aos_mutex_info_t;

/*
 * Create a mutex.
 *
 * The mutex is created in the unlocked state.
 *
 * mutex_id:
 *     Receives the portable AOS mutex ID.
 *
 * name:
 *     Human-readable mutex name.
 *
 * flags:
 *     Reserved for future use. Pass AOS_MUTEX_FLAG_NONE.
 */
int32_t AOS_MutexCreate(
    aos_mutex_t *mutex_id,
    const char *name,
    uint32_t flags);

/*
 * Delete a mutex.
 *
 * The mutex must not currently be owned or waited upon.
 */
int32_t AOS_MutexDelete(
    aos_mutex_t mutex_id);

/*
 * Acquire a mutex.
 *
 * Blocks indefinitely until the mutex becomes available.
 */
int32_t AOS_MutexLock(
    aos_mutex_t mutex_id);


/*
 * Attempt to acquire a mutex without blocking.
 *
 * Returns:
 *
 *     AOS_SUCCESS
 *     AOS_ERR_BUSY
 *     AOS_ERR_INVALID_ID
 */
int32_t AOS_MutexTryLock(
    aos_mutex_t mutex_id);


/*
 * Attempt to acquire a mutex for up to timeout_ms.
 *
 * timeout_ms is a relative timeout from the caller's point of view.
 */
int32_t AOS_MutexTimedLock(
    aos_mutex_t mutex_id,
    uint32_t timeout_ms);


/*
 * Release a mutex owned by the current task/thread.
 */
int32_t AOS_MutexUnlock(
    aos_mutex_t mutex_id);


/*
 * Obtain portable mutex information.
 */
int32_t AOS_MutexGetInfo(
    aos_mutex_t mutex_id,
    aos_mutex_info_t *info);


#ifdef __cplusplus
}
#endif

#endif /*AOS_MUTEX_H*/