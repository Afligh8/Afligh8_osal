#include "aos_osal.h"
#include "aos_task.h"
#include "aos_mutex.h"

#include <stdbool.h>
#include <stdio.h>
#include <semaphore.h>

typedef struct {
    aos_id_t mutex_id;
    sem_t done;
    int32_t result;
} mutex_test_context_t;

static volatile bool g_timed_done = false;

static volatile int32_t g_timed_result = AOS_ERR_GENERIC;

static volatile bool g_lock_done = false;

static volatile int g_shared_value = 0;

static void trylock_waiter(void *arg)
{
    mutex_test_context_t *ctx =
        (mutex_test_context_t *)arg;

    ctx->result =
        AOS_MutexTryLock(ctx->mutex_id);

    /*
     * Unexpected success: release it so the
     * test environment remains sane.
     */
    if (ctx->result == AOS_SUCCESS)
    {
        (void)AOS_MutexUnlock(ctx->mutex_id);
    }

    sem_post(&ctx->done);
}

static void wrong_owner_unlock(void *arg)
{
    mutex_test_context_t *ctx =
        (mutex_test_context_t *)arg;

    /*
     * Main thread owns the mutex.
     *
     * This task must NOT be permitted to unlock it.
     */
    ctx->result =
        AOS_MutexUnlock(ctx->mutex_id);

    sem_post(&ctx->done);
}

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

int main(void)
{
    aos_id_t mutex_id = AOS_ID_NONE;
    aos_id_t task_id  = AOS_ID_NONE;

    mutex_test_context_t ctx;

    aos_mutex_info_t info;

    int32_t status;

    unsigned int i;


    /*
     * Test synchronization semaphore.
     *
     * This is POSIX-only test infrastructure,
     * not part of the AOS public API.
     */
    ctx.result = AOS_ERR_GENERIC;

    if (sem_init(&ctx.done, 0, 0) != 0)
    {
        fprintf(stderr, "sem_init failed\n");
        return 1;
    }


    /*
     * Initialize OSAL.
     */
    status = AOS_Init();

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "Failed to initialize OSAL: %s\n",
                AOS_StrError(status));

        return 2;
    }


    /*
     * Create mutex.
     */
    status = AOS_MutexCreate(
        &mutex_id,
        "test-mutex",
        AOS_MUTEX_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "AOS_MutexCreate failed: %s\n",
                AOS_StrError(status));

        return 3;
    }


    /*
     * Only assign the context ID AFTER the mutex exists.
     */
    ctx.mutex_id = mutex_id;


    /*
     * Verify packed object type.
     */
    if (AOS_IdType(mutex_id) != AOS_TYPE_MUTEX)
    {
        return 4;
    }


    /*
     * Verify information lookup.
     */
    status = AOS_MutexGetInfo(
        mutex_id,
        &info);

    if (status != AOS_SUCCESS)
    {
        return 5;
    }


    /*
     * MAIN owns the mutex from this point.
     */
    status = AOS_MutexLock(mutex_id);

    if (status != AOS_SUCCESS)
    {
        return 6;
    }


    /*
     * TEST 1: TimedLock must timeout.
     */
    g_timed_done   = false;
    g_timed_result = AOS_ERR_GENERIC;

    status = AOS_TaskCreate(
        &task_id,
        "mutex-timeout",
        timed_waiter,
        &mutex_id,
        NULL,
        64u * 1024u,
        AOS_TASK_PRIORITY_DEFAULT,
        AOS_TASK_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        return 7;
    }


    for (i = 0u;
         i < 200u && !g_timed_done;
         ++i)
    {
        (void)AOS_TaskDelay(1u);
    }


    if (!g_timed_done)
    {
        fprintf(stderr,
                "Timed-lock waiter did not complete\n");

        return 8;
    }


    if (g_timed_result != AOS_ERR_TIMEOUT)
    {
        fprintf(stderr,
                "TimedLock: expected AOS_ERR_TIMEOUT, got %s\n",
                AOS_StrError(g_timed_result));

        return 9;
    }


    printf("Timed lock timeout test: PASS\n");


    /*
     * Reap the completed timed waiter.
     */
    if (AOS_TaskDelete(task_id) != AOS_SUCCESS)
    {
        return 10;
    }


    /*
     * TEST 2: TryLock from another task must be BUSY.
     *
     * MAIN STILL OWNS THE MUTEX.
     */
    ctx.result = AOS_ERR_GENERIC;

    status = AOS_TaskCreate(
        &task_id,
        "mutex-try",
        trylock_waiter,
        &ctx,
        NULL,
        64u * 1024u,
        AOS_TASK_PRIORITY_DEFAULT,
        AOS_TASK_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        return 11;
    }


    if (sem_wait(&ctx.done) != 0)
    {
        return 12;
    }


    if (ctx.result != AOS_ERR_BUSY)
    {
        fprintf(stderr,
                "TryLock: expected AOS_ERR_BUSY, got %s\n",
                AOS_StrError(ctx.result));

        return 13;
    }


    printf("TryLock busy test: PASS\n");


    if (AOS_TaskDelete(task_id) != AOS_SUCCESS)
    {
        return 14;
    }


    /*
     * TEST 3: Cannot delete an owned mutex.
     */
    status = AOS_MutexDelete(mutex_id);

    if (status != AOS_ERR_BUSY)
    {
        fprintf(stderr,
                "Busy delete: expected AOS_ERR_BUSY, got %s\n",
                AOS_StrError(status));

        return 15;
    }


    printf("Busy delete test: PASS\n");


    /*
     * TEST 4: Another task cannot unlock MAIN's mutex.
     */
    ctx.result = AOS_ERR_GENERIC;

    status = AOS_TaskCreate(
        &task_id,
        "mutex-wrong-unlock",
        wrong_owner_unlock,
        &ctx,
        NULL,
        64u * 1024u,
        AOS_TASK_PRIORITY_DEFAULT,
        AOS_TASK_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        return 16;
    }


    if (sem_wait(&ctx.done) != 0)
    {
        return 17;
    }


    if (ctx.result != AOS_ERR_INVALID_STATE)
    {
        fprintf(stderr,
                "Wrong-owner unlock: expected "
                "AOS_ERR_INVALID_STATE, got %s\n",
                AOS_StrError(ctx.result));

        return 18;
    }


    printf("Wrong-owner unlock test: PASS\n");


    if (AOS_TaskDelete(task_id) != AOS_SUCCESS)
    {
        return 19;
    }


    /*
     * MAIN legitimately releases the mutex.
     */
    status = AOS_MutexUnlock(mutex_id);

    if (status != AOS_SUCCESS)
    {
        return 20;
    }


    /*
     * TEST 5: A new task can now acquire the mutex.
     */
    g_lock_done    = false;
    g_shared_value = 0;


    status = AOS_TaskCreate(
        &task_id,
        "mutex-lock",
        normal_waiter,
        &mutex_id,
        NULL,
        64u * 1024u,
        AOS_TASK_PRIORITY_DEFAULT,
        AOS_TASK_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        return 21;
    }


    for (i = 0u;
         i < 200u && !g_lock_done;
         ++i)
    {
        (void)AOS_TaskDelay(1u);
    }


    if (!g_lock_done)
    {
        return 22;
    }


    if (g_shared_value != 42)
    {
        return 23;
    }


    printf("Normal lock/unlock test: PASS\n");


    if (AOS_TaskDelete(task_id) != AOS_SUCCESS)
    {
        return 24;
    }


    /*
     * TEST 6: Delete unlocked mutex.
     */
    status = AOS_MutexDelete(mutex_id);

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "AOS_MutexDelete failed: %s\n",
                AOS_StrError(status));

        return 25;
    }


    printf("Mutex delete test: PASS\n");


    /*
     * TEST 7: Old handle must now be stale.
     */
    status = AOS_MutexLock(mutex_id);

    if (status != AOS_ERR_INVALID_ID)
    {
        fprintf(stderr,
                "Stale ID: expected AOS_ERR_INVALID_ID, got %s\n",
                AOS_StrError(status));

        return 26;
    }


    printf("Stale ID test: PASS\n");


    /*
     * Test semaphore cleanup.
     */
    if (sem_destroy(&ctx.done) != 0)
    {
        return 27;
    }

    puts("ALL MUTEX TESTS PASSED");

    return 0;
}