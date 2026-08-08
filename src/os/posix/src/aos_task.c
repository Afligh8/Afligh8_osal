#include "aos_task.h"
#include <pthread.h>

// Static pool of TCBs in RAM (compile-time allocation)
static TaskContext_t task_pool[AOS_MAX_TASKS];

//  THE TRAMPOLINE FUNCTION
//  Matches POSIX signature: void* func(void*)
static void* internal_thread_trampoline(void* arg) {
    // Unpack context pointer
    TaskContext_t* ctx = (TaskContext_t*)arg;
    void (*real_task)(void) = ctx->user_entry;

    if (real_task != NULL){
        real_task();
    }
    

    // Call the actual OSAL entry point (void (*)(void))
    if (real_task != NULL) {
        real_task(); // Execute the actual embedded task loop
    }

    // Mark slot free when task finishes (if task ever returns)
    ctx->in_use = false;

    return NULL;
}


int32_t AOS_TaskCreate(const char *name, aos_id_t *task_id, void (*entry)(void), 
                        void *stack, size_t stack_size ){
    if (entry == NULL){
        return AOS_ERR_INVALID_PARAM;
    }
    
    // Find an unused slot in static RAM pool (O(N) deterministic search)
    TaskContext_t* ctx = NULL;
    for (int i = 0; i < AOS_MAX_TASKS; i++){  
        if (!task_pool[i].in_use){

            task_pool[i].in_use = true;

            ctx = &task_pool[i];

            //Store the slot index as the task_id handle
            if (task_id != NULL){
                *task_id = (aos_id_t)i;
            }
            break;
        }
    }
    // Handle pool  exhaustion
    if (ctx == NULL){
        return AOS_ERR_NO_FREE_IDS;
    }

    // Store user entry function inside the static TCB
    ctx->user_entry = entry;

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_t *attr_ptr = NULL;

    // Configure custom stack buffer if provided
    if (stack != NULL && stack_size > 0){
        pthread_attr_init(&attr);
        pthread_attr_setstack(&attr, stack, stack_size);
        attr_ptr = &attr;
    }
    
    // Launch thread via Trampoline, passing static context 'ctx'
    int status = pthread_create(&thread, attr_ptr, internal_thread_trampoline, (void*)ctx);

    if (attr_ptr != NULL){
        pthread_attr_destroy(&attr);
    }

    if (status != 0){
        ctx->in_use = false; // Rollback slot reservation on failure
        return AOS_ERR_CREATION_FAILED;
    }
    
    return AOS_SUCCESS;
}