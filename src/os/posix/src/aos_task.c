#include "aos_task.h"

#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

typedef struct {
    pthread_t           thread;
    aos_task_entry_t    entry;
    void               *arg;
    void               *stack;
    size_t              stack_size;
    aos_id_t            id;
    aos_task_priority_t priority;
    aos_task_state_t    state;
    uint8_t             serial;
    bool                in_use;
    char                name[AOS_MAX_NAME];
} aos_posix_task_slot_t;

static aos_posix_task_slot_t g_task_pool[AOS_MAX_TASKS];
static pthread_mutex_t       g_task_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t         g_task_key;
static bool                  g_task_key_valid = false;
static bool                  g_task_module_initialized = false;

static bool g_rt_enabled = false;
static int  g_rt_policy = SCHED_OTHER;
static int  g_rt_priority_min = 0;
static int  g_rt_priority_max = 0;

static bool AOS_PosixPolicyHasEnoughLevels(int policy, int *min_priority, int *max_priority)
{
    int min = sched_get_priority_min(policy);
    int max = sched_get_priority_max(policy);

    if (min < 0 || max < 0) {
        return false;
    }
    if (min >= max) {
        return false;
    }

    if ((unsigned)(max - min + 1) < AOS_CONFIG_TASK_PRIORITY_LEVELS) {
        return false;
    }

    if (min_priority) {
        *min_priority = min;
    }
    if (max_priority) {
        *max_priority = max;
    }
    return true;
}

static int AOS_PosixMapPriority(aos_task_priority_t aos_priority)
{
    // if (!g_rt_enabled) {
    //     return 0; // No mapping needed for non-RT policy
    // }

    int range = g_rt_priority_max - g_rt_priority_min;

    if (AOS_TASK_PRIORITY_LOWEST == 0u) {
        return g_rt_priority_max;
    }

    /* AOS: 0 is highest. POSIX RT: larger numeric value is higher. */
    int posix_priority = g_rt_priority_max - (int)(((unsigned)range * (unsigned)aos_priority) / (unsigned)(AOS_TASK_PRIORITY_LOWEST));
    return posix_priority;
}

static int32_t AOS_PosixConfigureRtPolicy(void)
{
    int fifo_min = 0;
    int fifo_max = 0;
    int rr_min = 0;
    int rr_max = 0;
    bool fifo_ok;
    bool rr_ok;
    int original_policy;
    struct sched_param original_param;
    struct sched_param probe_param;
    int rc;

    fifo_ok = AOS_PosixPolicyHasEnoughLevels(SCHED_FIFO, &fifo_min, &fifo_max);
    rr_ok   = AOS_PosixPolicyHasEnoughLevels(SCHED_RR, &rr_min, &rr_max);

    if (fifo_ok) {
        g_rt_policy = SCHED_FIFO;
        g_rt_priority_min = fifo_min;
        g_rt_priority_max = fifo_max;
    } else if (rr_ok) {
        g_rt_policy = SCHED_RR;
        g_rt_priority_min = rr_min;
        g_rt_priority_max = rr_max;
    } else {
#if AOS_CONFIG_POSIX_RT_REQUIRED
        return AOS_ERR_CREATION_FAILED;
#else
        g_rt_enabled = false;
        return AOS_SUCCESS;
#endif
    }

    rc = pthread_getschedparam(pthread_self(), &original_policy, &original_param);
    if (rc != 0) {
#if AOS_CONFIG_POSIX_RT_REQUIRED
        return AOS_ERR_CREATION_FAILED;
#else
        g_rt_enabled = false;
        return AOS_SUCCESS;
#endif
    }

    probe_param.sched_priority = g_rt_priority_min;
    rc = pthread_setschedparam(pthread_self(), g_rt_policy, &probe_param);
    if (rc == 0) {
        /*
         * The RT scheduling probe succeeded.
         *
         * We only changed the current thread temporarily to verify
         * that this process has permission to use the selected
         * real-time scheduling policy.
         */
        g_rt_enabled = true;

        /*
        * Restore the original scheduler settings for the calling
        * thread. Actual AOS tasks will receive their RT policy and
        * priority when AOS_TaskCreate() is called.
        */

        rc = pthread_setschedparam(pthread_self(), original_policy, &original_param);

        if (rc != 0) {
            fprintf(stderr,
                    "AOS: failed to restore original scheduler: %s\n",
                    strerror(rc));

            return AOS_ERR_CREATION_FAILED;
        }

        return AOS_SUCCESS;
    }

#if AOS_CONFIG_POSIX_RT_REQUIRED
    return AOS_ERR_CREATION_FAILED;
#else
    g_rt_enabled = false;
#if AOS_CONFIG_POSIX_RT_WARN_FALLBACK
    fprintf(stderr,
            "AOS: POSIX real-time scheduling unavailable (%s); falling back to SCHED_OTHER\n",
            strerror(rc));
#endif
    return AOS_SUCCESS;
#endif
}

int32_t AOS_PosixTaskInit(void)
{
    int rc;

    pthread_mutex_lock(&g_task_lock);
    if (g_task_module_initialized) {
        pthread_mutex_unlock(&g_task_lock);
        return AOS_SUCCESS;
    }

    memset(g_task_pool, 0, sizeof(g_task_pool));

    rc = pthread_key_create(&g_task_key, NULL);
    if (rc != 0) {
        pthread_mutex_unlock(&g_task_lock);
        return AOS_ERR_CREATION_FAILED;
    }
    g_task_key_valid = true;
    pthread_mutex_unlock(&g_task_lock);

    rc = AOS_PosixConfigureRtPolicy();
    if (rc != AOS_SUCCESS) {
        pthread_mutex_lock(&g_task_lock);
        (void)pthread_key_delete(g_task_key);
        g_task_key_valid = false;
        pthread_mutex_unlock(&g_task_lock);
        return rc;
    }

    pthread_mutex_lock(&g_task_lock);
    g_task_module_initialized = true;
    pthread_mutex_unlock(&g_task_lock);
    return AOS_SUCCESS;
}

// static int32_t AOS_PosixValidateTaskIdLocked(aos_id_t task_id, aos_posix_task_slot_t **out_slot)
// {
//     uint16_t index;
//     aos_posix_task_slot_t *slot;

//     if (AOS_IdType(task_id) != AOS_MAX_TASKS) {
//         return AOS_ERR_INVALID_ID;
//     }

//     index = AOS_IdIndex(task_id);
//     if (index >= AOS_MAX_TASKS) {
//         return AOS_ERR_INVALID_ID;
//     }


//     slot = &g_task_pool[index];
//     if (!slot->in_use || slot->id != task_id || slot->serial != AOS_IdSerial(task_id)) {
//         return AOS_ERR_INVALID_ID;
//     }

//     *out_slot = slot;
//     return AOS_SUCCESS;
// }

static int32_t AOS_PosixValidateTaskIdLocked(
    aos_id_t task_id,
    aos_posix_task_slot_t **slot_out)
{
    uint16_t index;
    aos_posix_task_slot_t *slot;

    if (slot_out == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (AOS_IdType(task_id) != AOS_TYPE_TASK) {
        return AOS_ERR_INVALID_ID;
    }

    index = AOS_IdIndex(task_id);

    if (index >= AOS_MAX_TASKS) {
        return AOS_ERR_INVALID_ID;
    }

    slot = &g_task_pool[index];

    if (!slot->in_use) {
        return AOS_ERR_INVALID_ID;
    }

    if (slot->id != task_id) {
        return AOS_ERR_INVALID_ID;
    }

    if (slot->serial != AOS_IdSerial(task_id)) {
        return AOS_ERR_INVALID_ID;
    }

    *slot_out = slot;

    return AOS_SUCCESS;
}

static void AOS_PosixTaskCleanup(void *arg)
{
    // if (slot == NULL) {
    //     return;
    // }

    // slot->in_use = false;
    // slot->id = 0;
    // slot->serial = 0;
    // slot->entry = NULL;
    // slot->arg = NULL;
    // slot->stack = NULL;
    // slot->stack_size = 0;
    // slot->priority = AOS_TASK_PRIORITY_NORMAL;
    // slot->state = AOS_TASK_STATE_READY;
    // memset(slot->name, 0, sizeof(slot->name));

    aos_posix_task_slot_t *slot = (aos_posix_task_slot_t *)arg;

    pthread_mutex_lock(&g_task_lock);
    if (slot->in_use){
        slot->state  = AOS_TASK_STATE_EXITED;
    }
    pthread_mutex_unlock(&g_task_lock);
    
}

static void *AOS_PosixTaskTrampoline(void *arg)
{
    aos_posix_task_slot_t *slot = (aos_posix_task_slot_t *)arg;
    aos_task_entry_t entry;
    void *entry_arg;

    if (g_task_key_valid) {
        (void)pthread_setspecific(g_task_key, (void *)(uintptr_t)slot->id);
    }

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    pthread_mutex_lock(&g_task_lock);
    slot->state = AOS_TASK_STATE_RUNNING;
    entry = slot->entry;
    entry_arg = slot->arg;
    pthread_mutex_unlock(&g_task_lock);

    pthread_cleanup_push(AOS_PosixTaskCleanup, slot);
    entry(entry_arg);
    pthread_cleanup_pop(1);

    return NULL;
}

static int AOS_PosixBuildThreadAttrs(pthread_attr_t *attr,
                                     void *stack,
                                     size_t stack_size,
                                     aos_task_priority_t priority)
{
    struct sched_param sched_param;
    int rc;

    rc = pthread_attr_init(attr);
    if (rc != 0) {
        return rc;
    }

    rc = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_JOINABLE);
    if (rc != 0) {
        pthread_attr_destroy(attr);
        return rc;
    }

    if (stack != NULL) {
        rc = pthread_attr_setstack(attr, stack, stack_size);
    } else {
        rc = pthread_attr_setstacksize(attr, stack_size);
    }
    if (rc != 0) {
        pthread_attr_destroy(attr);
        return rc;
    }

    if (g_rt_enabled) {
        rc = pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
        if (rc == 0) {
            rc = pthread_attr_setschedpolicy(attr, g_rt_policy);
        }
        if (rc == 0) {
            sched_param.sched_priority = AOS_PosixMapPriority(priority);
            rc = pthread_attr_setschedparam(attr, &sched_param);
        }
        if (rc != 0) {
            pthread_attr_destroy(attr);
            return rc;
        }
    }

    return 0;
}

int32_t AOS_TaskCreate(aos_id_t *task_id,
                       const char *name,
                       aos_task_entry_t entry,
                       void *arg,
                       void *stack,
                       size_t stack_size,
                       aos_task_priority_t priority,
                       uint32_t flags)
{
    aos_posix_task_slot_t *slot = NULL;
    pthread_attr_t attr;
    size_t name_len;
    uint16_t index;
    int rc;

    if (!g_task_module_initialized) {
        return AOS_ERR_GENERIC;
    }
    if (task_id == NULL || name == NULL || entry == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }
    if (stack_size == 0u) {
        return AOS_ERR_INVALID_SIZE;
    }
    if (priority > AOS_TASK_PRIORITY_LOWEST) {
        return AOS_ERR_INVALID_PRIORITY;
    }
    if ((flags & ~AOS_TASK_VALID_FLAGS) != 0u) {
        return AOS_ERR_INVALID_PARAM;
    }
    
    name_len = strlen(name);
    if (name_len >= AOS_MAX_NAME) {
        return AOS_ERR_NAME_TOO_LONG;
    }

    pthread_mutex_lock(&g_task_lock);

    for (index = 0; index < AOS_MAX_TASKS; ++index) {
        if (g_task_pool[index].in_use && strcmp(g_task_pool[index].name, name) == 0) {
            pthread_mutex_unlock(&g_task_lock);
            return AOS_ERR_NAME_TAKEN;
        }
    }

    for (index = 0; index < AOS_MAX_TASKS; ++index) {
        if (!g_task_pool[index].in_use) {
            slot = &g_task_pool[index];
            break;
        }
    }

    if (slot == NULL) {
        pthread_mutex_unlock(&g_task_lock);
        return AOS_ERR_NO_FREE_IDS;
    }
    slot->serial++;
    if (slot->serial == 0u) {
        slot->serial = 1u;
    }

    slot->id = AOS_IdPack(AOS_TYPE_TASK, slot->serial, index);
    slot->entry = entry;
    slot->arg = arg;
    slot->stack = stack;
    slot->stack_size = stack_size;
    slot->priority = priority;
    slot->state = AOS_TASK_STATE_STARTING;
    slot->in_use = true;
    memcpy(slot->name, name, name_len + 1u);
    *task_id = slot->id;

    pthread_mutex_unlock(&g_task_lock);

    rc = AOS_PosixBuildThreadAttrs(&attr, stack, stack_size, priority);
    if (rc != 0) {
        pthread_mutex_lock(&g_task_lock);
        slot->in_use = false;
        slot->state = AOS_TASK_STATE_UNUSED;
        pthread_mutex_unlock(&g_task_lock);
        *task_id = AOS_ID_NONE;
        return (rc == EINVAL) ? AOS_ERR_INVALID_SIZE : AOS_ERR_CREATION_FAILED;
    }

    rc = pthread_create(&slot->thread, &attr, AOS_PosixTaskTrampoline, slot);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        pthread_mutex_lock(&g_task_lock);
        slot->in_use = false;
        slot->state = AOS_TASK_STATE_UNUSED;
        pthread_mutex_unlock(&g_task_lock);
        *task_id = AOS_ID_NONE;
        return AOS_ERR_CREATION_FAILED;
    }

    return AOS_SUCCESS;
}

int32_t AOS_TaskDelete(aos_id_t task_id)
{
    aos_posix_task_slot_t *slot;
    pthread_t thread;
    int32_t status;
    int rc_cancel;
    int rc_join;

    pthread_mutex_lock(&g_task_lock);
    status = AOS_PosixValidateTaskIdLocked(task_id, &slot);
    if (status != AOS_SUCCESS) {
        pthread_mutex_unlock(&g_task_lock);
        return status;
    }
    thread = slot->thread;
    pthread_mutex_unlock(&g_task_lock);

    if (pthread_equal(pthread_self(), thread)) {
        return AOS_ERR_INVALID_PARAM;
    }

    rc_cancel = pthread_cancel(thread);
    rc_join = pthread_join(thread, NULL);

    if (rc_join != 0 && rc_join != ESRCH) {
        return AOS_ERR_GENERIC;
    }
    if (rc_cancel != 0 && rc_cancel != ESRCH) {
        return AOS_ERR_GENERIC;
    }

    pthread_mutex_lock(&g_task_lock);
    if (slot->in_use && slot->id == task_id) {
        slot->in_use = false;
        slot->state = AOS_TASK_STATE_UNUSED;
        slot->entry = NULL;
        slot->arg = NULL;
        slot->stack = NULL;
        slot->stack_size = 0u;
        slot->name[0] = '\0';
    }
    pthread_mutex_unlock(&g_task_lock);

    return AOS_SUCCESS;
}

void AOS_TaskExit(void)
{
    pthread_exit(NULL);
}

int32_t AOS_TaskDelay(uint32_t ms)
{
    struct timespec deadline;
    int rc;

    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        return AOS_ERR_GENERIC;
    }

    deadline.tv_sec += (time_t)(ms / 1000u);
    deadline.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    do {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    } while (rc == EINTR);

    return (rc == 0) ? AOS_SUCCESS : AOS_ERR_GENERIC;
}

int32_t AOS_TaskYield(void)
{
    return (sched_yield() == 0) ? AOS_SUCCESS : AOS_ERR_GENERIC;
}

int32_t AOS_TaskSetPriority(aos_id_t task_id, aos_task_priority_t priority)
{
    aos_posix_task_slot_t *slot;
    pthread_t thread;
    int32_t status;
    int rc = 0;

    if (priority > AOS_TASK_PRIORITY_LOWEST) {
        return AOS_ERR_INVALID_PRIORITY;
    }

    pthread_mutex_lock(&g_task_lock);
    status = AOS_PosixValidateTaskIdLocked(task_id, &slot);
    if (status != AOS_SUCCESS) {
        pthread_mutex_unlock(&g_task_lock);
        return status;
    }

    thread = slot->thread;
    if (g_rt_enabled) {
        rc = pthread_setschedprio(thread, AOS_PosixMapPriority(priority));
    }
    if (rc == 0) {
        slot->priority = priority;
    }
    pthread_mutex_unlock(&g_task_lock);

    return (rc == 0) ? AOS_SUCCESS : AOS_ERR_GENERIC;
}


int32_t AOS_TaskGetId(aos_id_t *task_id)
{
    void *value;

    if (task_id == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }
    if (!g_task_key_valid) {
        return AOS_ERR_INVALID_ID;
    }

    value = pthread_getspecific(g_task_key);
    if (value == NULL) {
        *task_id = AOS_ID_NONE;
        return AOS_ERR_INVALID_ID;
    }

    *task_id = (aos_id_t)(uintptr_t)value;
    return AOS_SUCCESS;
}


int32_t AOS_TaskGetInfo(
    aos_id_t task_id,
    aos_task_info_t *info)
{
    aos_posix_task_slot_t *slot;
    int32_t status;

    if (info == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    pthread_mutex_lock(&g_task_lock);

    status =
        AOS_PosixValidateTaskIdLocked(
            task_id,
            &slot
        );

    if (status == AOS_SUCCESS) {

        memcpy(
            info->name,
            slot->name,
            AOS_MAX_NAME
        );

        info->stack_size = slot->stack_size;
        info->priority   = slot->priority;
        info->state      = slot->state;
    }

    pthread_mutex_unlock(&g_task_lock);

    return status;
}

