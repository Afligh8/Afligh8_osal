#include "aos_osal.h"
#include "aos_task.h"
#include "aos_sem.h"

#include <stdbool.h>
#include <stdio.h>

/* NSH reports only 0/1 for a builtin's exit status, so say which check failed. */
#define AOS_TEST_FAIL(n) \
    do { fprintf(stderr, "%s: FAILED at check %d\n", __FILE__, (int)(n)); return (n); } while (0)

static volatile bool g_worker_started = false;

static volatile bool g_worker_done = false;

static volatile int g_shared_value = 0;

static volatile int32_t g_worker_result = AOS_ERR_GENERIC;

static void blocking_waiter(void *arg)
{
    aos_sem_t sem_id =
        *(aos_sem_t *)arg;

    g_worker_started = true;

    g_worker_result =
        AOS_SemWait(sem_id);

    if (g_worker_result == AOS_SUCCESS)
    {
        g_shared_value = 42;
    }

    g_worker_done = true;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    aos_sem_t sem_id      = AOS_SEM_NONE;
    aos_sem_t other_sem_id = AOS_SEM_NONE;
    aos_task_t task_id     = AOS_TASK_NONE;

    aos_sem_info_t info;

    int32_t status;

    unsigned int i;


    /*
     * Initialize OSAL.
     */
    status = AOS_Init();

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "Failed to initialize OSAL: %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(1);
    }


    /*
     * TEST: parameter validation.
     */
    status = AOS_SemCreate(NULL, "bad", AOS_SEM_EMPTY, AOS_SEM_FLAG_NONE);

    if (status != AOS_ERR_INVALID_POINTER)
    {
        AOS_TEST_FAIL(2);
    }

    status = AOS_SemCreate(&sem_id, NULL, AOS_SEM_EMPTY, AOS_SEM_FLAG_NONE);

    if (status != AOS_ERR_INVALID_POINTER)
    {
        AOS_TEST_FAIL(3);
    }

    status = AOS_SemCreate(&sem_id, "", AOS_SEM_EMPTY, AOS_SEM_FLAG_NONE);

    if (status != AOS_ERR_INVALID_PARAM)
    {
        AOS_TEST_FAIL(4);
    }

    status = AOS_SemCreate(
        &sem_id,
        "this-name-is-definitely-too-long-for-aos",
        AOS_SEM_EMPTY,
        AOS_SEM_FLAG_NONE);

    if (status != AOS_ERR_NAME_TOO_LONG)
    {
        AOS_TEST_FAIL(5);
    }

    status = AOS_SemCreate(&sem_id, "bad-flags", AOS_SEM_EMPTY, 0xFFu);

    if (status != AOS_ERR_INVALID_PARAM)
    {
        AOS_TEST_FAIL(6);
    }

    status = AOS_SemCreate(&sem_id, "bad-state", (aos_sem_state_t)7, AOS_SEM_FLAG_NONE);

    if (status != AOS_ERR_INVALID_PARAM)
    {
        AOS_TEST_FAIL(7);
    }

    printf("Parameter validation test: PASS\n");


    /*
     * Create the real test semaphore, starting EMPTY.
     */
    status = AOS_SemCreate(
        &sem_id,
        "test-sem",
        AOS_SEM_EMPTY,
        AOS_SEM_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "AOS_SemCreate failed: %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(8);
    }

    if (AOS_IdType(sem_id.id) != AOS_TYPE_BINSEM)
    {
        AOS_TEST_FAIL(9);
    }


    /*
     * TEST: duplicate name.
     */
    status = AOS_SemCreate(
        &other_sem_id,
        "test-sem",
        AOS_SEM_EMPTY,
        AOS_SEM_FLAG_NONE);

    if (status != AOS_ERR_NAME_TAKEN)
    {
        fprintf(stderr,
                "Duplicate name: expected AOS_ERR_NAME_TAKEN, got %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(10);
    }

    printf("Duplicate name test: PASS\n");


    /*
     * TEST: GetInfo.
     */
    status = AOS_SemGetInfo(sem_id, &info);

    if (status != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(11);
    }

    printf("GetInfo test: PASS\n");


    /*
     * TEST: TryWait on an empty semaphore must be BUSY.
     */
    status = AOS_SemTryWait(sem_id);

    if (status != AOS_ERR_BUSY)
    {
        fprintf(stderr,
                "TryWait empty: expected AOS_ERR_BUSY, got %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(12);
    }

    printf("TryWait empty test: PASS\n");


    /*
     * TEST: TimedWait on an empty semaphore must time out.
     */
    status = AOS_SemTimedWait(sem_id, 20u);

    if (status != AOS_ERR_TIMEOUT)
    {
        fprintf(stderr,
                "TimedWait empty: expected AOS_ERR_TIMEOUT, got %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(13);
    }

    printf("TimedWait timeout test: PASS\n");


    /*
     * TEST: Post, then TryWait must succeed and consume it.
     */
    status = AOS_SemPost(sem_id);

    if (status != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(14);
    }


    /*
     * TEST: strict binary semantics -- posting an already-full
     * semaphore must be rejected, not silently absorbed.
     */
    status = AOS_SemPost(sem_id);

    if (status != AOS_ERR_INVALID_STATE)
    {
        fprintf(stderr,
                "Double post: expected AOS_ERR_INVALID_STATE, got %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(15);
    }

    printf("Double post rejection test: PASS\n");

    status = AOS_SemTryWait(sem_id);

    if (status != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(16);
    }

    /*
     * Consumed: must be empty again.
     */
    status = AOS_SemTryWait(sem_id);

    if (status != AOS_ERR_BUSY)
    {
        AOS_TEST_FAIL(17);
    }

    printf("Post/TryWait consume test: PASS\n");


    /*
     * TEST: a task genuinely blocked in AOS_SemWait must be released by
     * AOS_SemPost, and observe the effect only after being posted.
     */
    g_worker_started = false;
    g_worker_done    = false;
    g_shared_value   = 0;
    g_worker_result  = AOS_ERR_GENERIC;

    status = AOS_TaskCreate(
        &task_id,
        "sem-waiter",
        blocking_waiter,
        &sem_id,
        NULL,
        64u * 1024u,
        AOS_TASK_PRIORITY_DEFAULT,
        AOS_TASK_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(18);
    }

    for (i = 0u;
         i < 200u && !g_worker_started;
         ++i)
    {
        (void)AOS_TaskDelay(1u);
    }

    if (!g_worker_started)
    {
        AOS_TEST_FAIL(19);
    }

    /*
     * Give the worker a fair chance to actually reach the blocking
     * syscall before checking that it's still blocked.
     */
    (void)AOS_TaskDelay(20u);

    if (g_worker_done)
    {
        fprintf(stderr,
                "Worker completed before being posted\n");

        AOS_TEST_FAIL(20);
    }


    /*
     * TEST: cannot delete a semaphore with a blocked waiter.
     */
    status = AOS_SemDelete(sem_id);

    if (status != AOS_ERR_BUSY)
    {
        fprintf(stderr,
                "Busy delete: expected AOS_ERR_BUSY, got %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(21);
    }

    printf("Busy delete test: PASS\n");


    status = AOS_SemPost(sem_id);

    if (status != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(22);
    }

    for (i = 0u;
         i < 200u && !g_worker_done;
         ++i)
    {
        (void)AOS_TaskDelay(1u);
    }

    if (!g_worker_done)
    {
        AOS_TEST_FAIL(23);
    }

    if (g_worker_result != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(24);
    }

    if (g_shared_value != 42)
    {
        AOS_TEST_FAIL(25);
    }

    printf("Blocking wait/post test: PASS\n");

    if (AOS_TaskDelete(task_id) != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(26);
    }


    /*
     * TEST: delete an idle semaphore.
     */
    status = AOS_SemDelete(sem_id);

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "AOS_SemDelete failed: %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(27);
    }

    printf("Semaphore delete test: PASS\n");


    /*
     * TEST: stale id must be rejected by every entry point.
     */
    if (AOS_SemWait(sem_id) != AOS_ERR_INVALID_ID)
    {
        AOS_TEST_FAIL(28);
    }

    if (AOS_SemPost(sem_id) != AOS_ERR_INVALID_ID)
    {
        AOS_TEST_FAIL(29);
    }

    printf("Stale ID test: PASS\n");


    /*
     * TEST: AOS_SEM_FULL creation must be immediately available.
     */
    status = AOS_SemCreate(
        &sem_id,
        "full-sem",
        AOS_SEM_FULL,
        AOS_SEM_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(30);
    }

    status = AOS_SemTryWait(sem_id);

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr,
                "Initial-full TryWait: expected AOS_SUCCESS, got %s\n",
                AOS_StrError(status));

        AOS_TEST_FAIL(31);
    }

    if (AOS_SemDelete(sem_id) != AOS_SUCCESS)
    {
        AOS_TEST_FAIL(32);
    }

    printf("Initial-full creation test: PASS\n");


    puts("ALL SEMAPHORE TESTS PASSED");

    return 0;
}
