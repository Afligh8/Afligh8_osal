#include "aos_osal.h"
#include "aos_task.h"
#include "aos_mutex.h"

#include <stdbool.h>
#include <stdio.h>

static volatile bool g_timed_done = false;

static volatile int32_t g_timed_result = AOS_ERR_GENERIC;

static volatile bool g_lock_done = false;

static volatile int g_shared_value = 0;

static void timed_waiter(void *arg)
{
    aos_id_t mutex_id = *(aos_id_t *)arg;

    /*
     * Store the actual OSAL result so the parent
     * can verify what happened.
     */
    g_timed_result =
        AOS_MutexTimedLock(
            mutex_id,
            50u);

    // printf(
    //     "waiter: TimedLock returned %d (%s)\n",
    //     (int)g_timed_result,
    //     AOS_StrError(g_timed_result));

    /*
     * If the lock unexpectedly succeeded, release it.
     */
    if (g_timed_result == AOS_SUCCESS)
    {
        (void)AOS_MutexUnlock(mutex_id);
    }

    /*
     * Mark the worker as complete only after the result
     * has been written.
     */
    g_timed_done = true;
}

static void normal_waiter(void *arg)
{
    aos_id_t mutex_id =
        *(aos_id_t *)arg;


    if (AOS_MutexLock(
            mutex_id) == AOS_SUCCESS) {

        g_shared_value = 42;

        (void)AOS_MutexUnlock(
            mutex_id);
    }

    g_lock_done = true;
}

int main(void){
    aos_id_t mutex_id = AOS_ID_NONE;

    aos_id_t task_id = AOS_ID_NONE;

    aos_mutex_info_t info;

    int32_t status;

    unsigned int i;

    status = AOS_Init();

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize OSAL: %s\n", AOS_StrError(status));
        return 1;
    }
    
    status = AOS_MutexCreate(
        &mutex_id,
        "test-mutex",
        AOS_MUTEX_FLAG_NONE);


    if (status != AOS_SUCCESS) {

        fprintf(
            stderr,
            "AOS_MutexCreate failed: %s\n",
            AOS_StrError(status));

        return 2;
    }

    if (AOS_IdType(mutex_id) !=
        AOS_TYPE_MUTEX) {

        return 3;
    }

    status =
        AOS_MutexGetInfo(
            mutex_id,
            &info);

    if (status != AOS_SUCCESS) {
        return 4;
    }
    
    /*
     * Main thread owns mutex.
     */
    status =
        AOS_MutexLock(
            mutex_id);

    if (status != AOS_SUCCESS) {
        return 5;
    }

    /*
     * Spawn a task that will attempt to acquire the mutex
     * with a timeout. It should fail with AOS_ERR_TIMEOUT.
     */
     /*
     * Another task should timeout.
     */
    status =
        AOS_TaskCreate(
            &task_id,
            "mutex-timeout",
            timed_waiter,
            &mutex_id,
            NULL,
            64u * 1024u,
            AOS_TASK_PRIORITY_DEFAULT,
            AOS_TASK_FLAG_NONE);


    if (status != AOS_SUCCESS) {
        return 6;
    }
    
    /*
     * Wait for the task to either timeout or complete.
     */
    for (i = 0u;
         i < 200u && !g_timed_done;
         ++i) {

        (void)AOS_TaskDelay(1u);
    }

    /*
     * Task should have timed out.
     */
    if (!g_timed_done) {
        return 7;
    }
    printf("Pass\n");

    /*
     * Task should have returned AOS_ERR_TIMEOUT.
     */
    if (g_timed_result !=
        AOS_ERR_TIMEOUT) {

        fprintf(
            stderr,
            "expected timeout, got %s\n",
            AOS_StrError(g_timed_result));

        return 8;
    }

    /*
     * Delete the task.
     */                                         
    if (AOS_TaskDelete(
            task_id) != AOS_SUCCESS) {

        return 9;
    }


    /*
     * Release mutex.
     */
    if (AOS_MutexUnlock(
            mutex_id) != AOS_SUCCESS) {

        return 10;
    }


    /*
     * New task should now acquire it.
     */
    g_lock_done = false;


    status =
        AOS_TaskCreate(
            &task_id,
            "mutex-lock",
            normal_waiter,
            &mutex_id,
            NULL,
            64u * 1024u,
            AOS_TASK_PRIORITY_DEFAULT,
            AOS_TASK_FLAG_NONE);


    if (status != AOS_SUCCESS) {
        return 11;
    }


    for (i = 0u;
         i < 200u && !g_lock_done;
         ++i) {

        (void)AOS_TaskDelay(1u);
    }


    if (!g_lock_done) {
        return 12;
    }


    if (g_shared_value != 42) {
        return 13;
    }


    if (AOS_TaskDelete(
            task_id) != AOS_SUCCESS) {

        return 14;
    }


    /*
     * Delete mutex.
     */
    status =
        AOS_MutexDelete(
            mutex_id);


    if (status != AOS_SUCCESS) {

        fprintf(
            stderr,
            "AOS_MutexDelete failed: %s\n",
            AOS_StrError(status));

        return 15;
    }


    /*
     * Stale ID must be rejected.
     */
    if (AOS_MutexLock(
            mutex_id) !=
        AOS_ERR_INVALID_ID) {

        return 16;
    }


    puts("ALL MUTEX TESTS PASSED");

    return 0;

}