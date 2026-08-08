#ifndef AOS_TASK_H
#define AOS_TASK_H

#include "aos_osal.h"
#include <stddef.h>
#include <stdbool.h>

/*
** These typedefs are for the task entry point
*/
typedef void aos_task;                      /**< @brief For task entry point */
typedef aos_task((*osal_task_entry)(void)); /**< @brief For task entry point */

// Context struct to store parameters passed to the thread
typedef struct {
    osal_task_entry user_entry; // Holds function pointer (void (*func)(void))
    // size_t stack_size;
    // uint32_t priority;
    // uint32_t flags;
    bool in_use;
} TaskContext_t;

int32_t AOS_TaskCreate(const char *name, aos_id_t *task_id, void (*entry)(void), 
                        void *stack, size_t stack_size);

int32_t AOS_TaskDelete(aos_id_t task_id);

int32_t AOS_TaskDelay(uint32_t ms);

#endif // AOS_TASK_H