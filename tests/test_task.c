#include "aos_task.h"
#include "aos_osal.h"

#include <stdio.h>
#include <stdbool.h>

static volatile int g_value = 0;
static volatile bool g_keep_running = true;

static aos_id_t g_seen_id = AOS_ID_NONE;


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

    puts("ALL TASK TESTS PASSED");
    return 0;
}

