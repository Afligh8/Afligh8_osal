#include "aos_task.h"
#include "aos_osal.h"

#include <stdio.h>
#include <stdbool.h>

static volatile int g_value = 0;
static volatile bool g_keep_running = true;

static aos_id_t g_seen_id = AOS_ID_NONE;

static volatile bool g_join_task_finished = false;

static volatile bool g_self_join_done = false;
static volatile int32_t g_self_join_result = AOS_ERR_GENERIC;


static void short_task(void *arg)
{
    (void)arg;

    /*
     * Give AOS_TaskJoin() something real to wait for: if Join returned
     * before this delay elapsed, g_join_task_finished would still be
     * false when the caller checks it.
     */
    (void)AOS_TaskDelay(20u);

    g_join_task_finished = true;
}

static void self_join_task(void *arg)
{
    aos_id_t self_id = AOS_ID_NONE;

    (void)arg;

    (void)AOS_TaskGetId(&self_id);

    g_self_join_result = AOS_TaskJoin(self_id);

    g_self_join_done = true;
}


static void test_task(void *arg)
{
    int *value = (int *)arg;

    /*
     * Confirm that the task can retrieve its own
     * AOS task ID through thread-local storage.
     */
    (void)AOS_TaskGetId(&g_seen_id);

    /*
     * Signal to the parent that this task has started.
     */
    g_value = *value;

    /*
     * Stay alive while the main test exercises
     * GetInfo() and SetPriority().
     */
    while (g_keep_running) {
        (void)AOS_TaskDelay(10);
    }
}

int main(void)
{
    aos_id_t task_id = AOS_ID_NONE;
    aos_task_info_t info;
    int value = 42;
    int i;


    if (AOS_Init() != AOS_SUCCESS) {
        fprintf(stderr, "Failed to initialize OSAL\n");
        return 1;
    }

    if (AOS_TaskCreate(&task_id,
                       "task-test",
                       test_task,
                       &value,
                       NULL,
                       64u * 1024u, //64 KB stack
                       AOS_TASK_PRIORITY_DEFAULT,
                       AOS_TASK_FLAG_NONE) != AOS_SUCCESS) {
        return 2;
    }

    for (i = 0; i < 100 && g_value != 42; ++i) {
        (void)AOS_TaskDelay(1);
    }

    if (g_value != 42) return 3;
    if (g_seen_id != task_id) return 4;
    if (AOS_IdType(task_id) != AOS_TYPE_TASK) return 5;
    if (AOS_TaskGetInfo(task_id, &info) != AOS_SUCCESS) return 6;
    if (AOS_TaskSetPriority(task_id, AOS_TASK_PRIORITY_HIGHEST) != AOS_SUCCESS) return 7;
    if (AOS_TaskDelete(task_id) != AOS_SUCCESS) return 8;
    if (AOS_TaskDelete(task_id) != AOS_ERR_INVALID_ID) return 9;


    /*
     * TEST: AOS_TaskJoin waits for natural completion, not just for the
     * task to have merely started or to be somewhere mid-run.
     */
    aos_id_t join_task_id = AOS_ID_NONE;

    g_join_task_finished = false;

    if (AOS_TaskCreate(&join_task_id,
                       "join-test",
                       short_task,
                       NULL,
                       NULL,
                       64u * 1024u,
                       AOS_TASK_PRIORITY_DEFAULT,
                       AOS_TASK_FLAG_NONE) != AOS_SUCCESS) {
        return 10;
    }

    if (AOS_TaskJoin(join_task_id) != AOS_SUCCESS) {
        return 11;
    }

    if (!g_join_task_finished) {
        fprintf(stderr, "Join returned before the task actually finished\n");
        return 12;
    }

    printf("Join waits for natural completion test: PASS\n");


    /*
     * TEST: joining a task a second time must fail, not silently
     * double-join the same pthread_t.
     */
    if (AOS_TaskJoin(join_task_id) != AOS_ERR_INVALID_STATE) {
        return 13;
    }

    printf("Double join rejection test: PASS\n");


    /*
     * TEST: Delete after Join must succeed without re-joining the
     * already-reaped thread.
     */
    if (AOS_TaskDelete(join_task_id) != AOS_SUCCESS) {
        return 14;
    }

    printf("Delete-after-join test: PASS\n");


    /*
     * TEST: joining a stale/unknown id must fail.
     */
    if (AOS_TaskJoin(join_task_id) != AOS_ERR_INVALID_ID) {
        return 15;
    }

    printf("Join stale id test: PASS\n");


    /*
     * TEST: a task cannot join itself.
     */
    g_self_join_done   = false;
    g_self_join_result = AOS_ERR_GENERIC;

    if (AOS_TaskCreate(&join_task_id,
                       "self-join",
                       self_join_task,
                       NULL,
                       NULL,
                       64u * 1024u,
                       AOS_TASK_PRIORITY_DEFAULT,
                       AOS_TASK_FLAG_NONE) != AOS_SUCCESS) {
        return 16;
    }

    for (i = 0; i < 200 && !g_self_join_done; ++i) {
        (void)AOS_TaskDelay(1);
    }

    if (!g_self_join_done) {
        return 17;
    }

    if (g_self_join_result != AOS_ERR_INVALID_PARAM) {
        fprintf(stderr,
                "Self join: expected AOS_ERR_INVALID_PARAM, got %s\n",
                AOS_StrError(g_self_join_result));
        return 18;
    }

    if (AOS_TaskJoin(join_task_id) != AOS_SUCCESS) {
        return 19;
    }

    if (AOS_TaskDelete(join_task_id) != AOS_SUCCESS) {
        return 20;
    }

    printf("Self join rejection test: PASS\n");


    puts("ALL TASK TESTS PASSED");
    return 0;
}

