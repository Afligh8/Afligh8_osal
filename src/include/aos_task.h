#ifndef AOS_TASK_H
#define AOS_TASK_H

#include "aos_osal.h"
#include "aos_config.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*aos_task_entry_t)(void *arg);
typedef uint8_t aos_task_priority_t;

#define AOS_TASK_PRIORITY_HIGHEST ((aos_task_priority_t)0u)
#define AOS_TASK_PRIORITY_LOWEST  ((aos_task_priority_t)(AOS_CONFIG_TASK_PRIORITY_LEVELS - 1u))
#define AOS_TASK_PRIORITY_DEFAULT ((aos_task_priority_t)(AOS_CONFIG_TASK_PRIORITY_LEVELS / 2u))

#define AOS_TASK_FLAG_NONE        0u
#define AOS_TASK_VALID_FLAGS      AOS_TASK_FLAG_NONE

typedef enum {
    AOS_TASK_STATE_UNUSED = 0,
    AOS_TASK_STATE_STARTING,
    AOS_TASK_STATE_RUNNING,
    AOS_TASK_STATE_EXITED
} aos_task_state_t;

typedef struct {
    char                name[AOS_MAX_NAME];
    size_t              stack_size;
    aos_task_priority_t priority;
    aos_task_state_t    state;
} aos_task_info_t;


int32_t AOS_TaskCreate(aos_id_t *task_id,
                       const char *name,
                       aos_task_entry_t entry,
                       void *arg,
                       void *stack,
                       size_t stack_size,
                       aos_task_priority_t priority,
                       uint32_t flags);

void AOS_TaskExit(void);

int32_t AOS_TaskDelete(aos_id_t task_id);

/*
 * Block until the target task exits -- either by returning from its
 * entry function or by calling AOS_TaskExit() -- without forcing it to
 * terminate.
 *
 * The task's AOS id remains valid after a successful Join: exactly like
 * a mutex or semaphore, the caller must still call AOS_TaskDelete()
 * afterward to release the slot for reuse. AOS_TaskDelete() detects a
 * task that was already joined and skips re-joining it.
 *
 * A given task must not be joined by more than one caller, and must not
 * be concurrently joined and deleted from different threads -- exactly
 * the restriction POSIX places on pthread_join(). A task joining itself
 * returns AOS_ERR_INVALID_PARAM; joining a task a second time (or after
 * it has already been deleted) returns AOS_ERR_INVALID_STATE /
 * AOS_ERR_INVALID_ID respectively.
 *
 * ISR-safe: no.  blocking: yes.
 */
int32_t AOS_TaskJoin(aos_id_t task_id);

int32_t AOS_TaskDelay(uint32_t ms);

int32_t AOS_TaskYield(void);

int32_t AOS_TaskSetPriority(aos_id_t task_id, aos_task_priority_t priority);

int32_t AOS_TaskGetId(aos_id_t *task_id);

int32_t AOS_TaskGetInfo(aos_id_t task_id, aos_task_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // AOS_TASK_H