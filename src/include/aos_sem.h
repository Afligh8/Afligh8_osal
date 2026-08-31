#ifndef AOS_SEM_H
#define AOS_SEM_H

#include "aos_osal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Binary semaphore creation flags.
 *
 * No optional behaviour is currently defined, but the flags field is
 * retained so the API can grow without changing AOS_SemCreate().
 */
#define AOS_SEM_FLAG_NONE 0u

/*
 * Initial state at creation.
 *
 *   AOS_SEM_EMPTY: the first AOS_SemWait() blocks until some AOS_SemPost().
 *   AOS_SEM_FULL:  the first AOS_SemWait() succeeds immediately.
 */
typedef enum {
    AOS_SEM_EMPTY = 0,
    AOS_SEM_FULL  = 1
} aos_sem_state_t;

/*
 * Information available for an AOS binary semaphore.
 *
 * creator contains the AOS task ID which created the semaphore when
 * creation occurred from an AOS-managed task.
 *
 * If the semaphore was created from the application's main thread or
 * another non-AOS thread, creator is AOS_ID_NONE.
 */
typedef struct
{
    char     name[AOS_MAX_NAME];
    aos_task_t creator;
} aos_sem_info_t;

/*
 * Create a binary semaphore.
 *
 * mutex_id:
 *     Receives the portable AOS semaphore ID.
 *
 * name:
 *     Human-readable semaphore name.
 *
 * initial_state:
 *     AOS_SEM_EMPTY or AOS_SEM_FULL.
 *
 * flags:
 *     Reserved for future use. Pass AOS_SEM_FLAG_NONE.
 *
 * ISR-safe: no.  blocking: no.
 */
int32_t AOS_SemCreate(
    aos_sem_t *sem_id,
    const char *name,
    aos_sem_state_t initial_state,
    uint32_t flags);

/*
 * Delete a binary semaphore.
 *
 * Fails with AOS_ERR_BUSY if a task is currently blocked in
 * AOS_SemWait/TryWait/TimedWait on it.
 *
 * The caller is responsible for ensuring no interrupt context can still
 * call AOS_SemPost() on this id once deletion begins — AOS_SemPost is
 * lock-free by design (see below) and cannot be held off by this call,
 * exactly as POSIX sem_destroy() requires no other party is still acting
 * on the semaphore.
 *
 * ISR-safe: no.  blocking: no.
 */
int32_t AOS_SemDelete(
    aos_sem_t sem_id);

/*
 * Block indefinitely until the semaphore is posted, then consume it
 * (state returns to AOS_SEM_EMPTY).
 *
 * ISR-safe: no.  blocking: yes.
 */
int32_t AOS_SemWait(
    aos_sem_t sem_id);

/*
 * Consume the semaphore only if it is already full.
 *
 * Returns:
 *
 *     AOS_SUCCESS
 *     AOS_ERR_BUSY (empty)
 *     AOS_ERR_INVALID_ID
 *
 * ISR-safe: no.  blocking: no.
 */
int32_t AOS_SemTryWait(
    aos_sem_t sem_id);

/*
 * Consume the semaphore, blocking for up to timeout_ms.
 *
 * timeout_ms is a relative timeout from the caller's point of view.
 *
 * ISR-safe: no.  blocking: yes (bounded).
 */
int32_t AOS_SemTimedWait(
    aos_sem_t sem_id,
    uint32_t timeout_ms);

/*
 * Post (signal) the semaphore.
 *
 * Strict binary semantics: posting an already-full semaphore is treated
 * as a usage bug, not silently absorbed — it returns AOS_ERR_INVALID_STATE
 * and does NOT touch the underlying wait count. This mirrors how
 * AOS_MutexLock surfaces EDEADLK instead of masking it.
 *
 * ISR-safe: yes. This call never blocks and never takes the internal
 * table lock; the id and full/empty state are validated using only
 * atomic operations, at the cost of a narrow, documented hazard: posting
 * to a semaphore that is concurrently being deleted (AOS_SemDelete) is a
 * use-after-free on the caller's part, not something this call guards
 * against.  blocking: no.
 */
int32_t AOS_SemPost(
    aos_sem_t sem_id);

/*
 * Obtain portable semaphore information.
 *
 * ISR-safe: no.  blocking: no.
 */
int32_t AOS_SemGetInfo(
    aos_sem_t sem_id,
    aos_sem_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* AOS_SEM_H */
