#include "aos_mutex.h"
#include "aos_task.h"

#include <pthread.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct
{
    pthread_mutex_t native_mutex;

    aos_id_t id;
    aos_id_t creator;

    uint32_t ref_count;

    uint8_t serial;

    bool in_use;

    char name[AOS_MAX_NAME];

} aos_posix_mutex_slot_t;


static aos_posix_mutex_slot_t
    g_mutex_pool[AOS_MAX_MUTEXES];


/*
 * Internal mutex protecting the object table itself.
 *
 * This mutex is NOT exposed through the AOS API.
 *
 * The critical sections protected by this object must remain very short.
 * Never block on an application mutex while holding this lock.
 */
static pthread_mutex_t g_mutex_table_lock = PTHREAD_MUTEX_INITIALIZER;

static bool g_mutex_initialized = false;

int32_t AOS_PosixMutexInit(void)
{
    if (g_mutex_initialized) {
        return AOS_SUCCESS;
    }

    memset(g_mutex_pool, 0, sizeof(g_mutex_pool));

    g_mutex_initialized = true;
    
    return AOS_SUCCESS;
}

static int32_t AOS_PosixValidateMutexIdLocked(
    aos_id_t mutex_id,
    aos_posix_mutex_slot_t **slot_out)
{
    uint16_t index;
    aos_posix_mutex_slot_t *slot;

    if (slot_out == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (AOS_IdType(mutex_id) != AOS_TYPE_MUTEX) {
        return AOS_ERR_INVALID_ID;
    }

    index = AOS_IdIndex(mutex_id);

    if (index >= AOS_MAX_MUTEXES) {
        return AOS_ERR_INVALID_ID;
    }

    slot = &g_mutex_pool[index];

    if (!slot->in_use) {
        return AOS_ERR_INVALID_ID;
    }

    if (slot->id != mutex_id) {
        return AOS_ERR_INVALID_ID;
    }

    if (slot->serial != AOS_IdSerial(mutex_id)) {
        return AOS_ERR_INVALID_ID;
    }

    *slot_out = slot;

    return AOS_SUCCESS;
}

static int32_t AOS_PosixMutexAcquireReference(
    aos_id_t mutex_id,
    aos_posix_mutex_slot_t **slot_out)
{
    aos_posix_mutex_slot_t *slot;
    int32_t status;

    pthread_mutex_lock(&g_mutex_table_lock);

    status =
        AOS_PosixValidateMutexIdLocked(
            mutex_id,
            &slot);

    if (status == AOS_SUCCESS) {

        ++slot->ref_count;

        *slot_out = slot;
    }

    pthread_mutex_unlock(&g_mutex_table_lock);

    return status;
}

static void AOS_PosixMutexReleaseReference(
    aos_posix_mutex_slot_t *slot)
{
    pthread_mutex_lock(&g_mutex_table_lock);

    if (slot->ref_count > 0u) {
        --slot->ref_count;
    }

    pthread_mutex_unlock(&g_mutex_table_lock);
}


int32_t AOS_MutexCreate(
        aos_id_t *mutex_id,
        const char *name,
        uint32_t flags)
{
    pthread_mutexattr_t attr;

    aos_posix_mutex_slot_t *slot = NULL;

    aos_id_t creator = AOS_ID_NONE;

    size_t name_length;

    uint16_t index;

    uint8_t next_serial;

    int rc; //return code from pthread calls


    if (mutex_id == NULL || name == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }


    if (flags != AOS_MUTEX_FLAG_NONE) {
        return AOS_ERR_INVALID_PARAM;
    }

    name_length = strlen(name);

    if (name_length == 0u) {
        return AOS_ERR_INVALID_PARAM;
    }

    if (name_length >= AOS_MAX_NAME) {
        return AOS_ERR_NAME_TOO_LONG;
    }

    /*
     * Creation can also happen from main(), which is not necessarily an
     * AOS-managed task. In that case creator remains AOS_ID_NONE.
     */
    (void)AOS_TaskGetId(&creator);


    pthread_mutex_lock(&g_mutex_table_lock);

    /*
     * Enforce unique names.
     */
    for (index = 0u;
         index < AOS_MAX_MUTEXES;
         ++index) {

        if (g_mutex_pool[index].in_use &&
            strcmp(g_mutex_pool[index].name, name) == 0) {

            pthread_mutex_unlock(
                &g_mutex_table_lock);

            return AOS_ERR_NAME_TAKEN;
        }
    }

    /*
     * Find a free static object slot.
     */
    for (index = 0u;
         index < AOS_MAX_MUTEXES;
         ++index) {

        if (!g_mutex_pool[index].in_use) {
            slot = &g_mutex_pool[index];
            break;
        }
    }

    if (slot == NULL) {

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        return AOS_ERR_NO_FREE_IDS;
    }

    rc = pthread_mutexattr_init(&attr);

    if (rc != 0) {

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        return AOS_ERR_CREATION_FAILED;
    }

    /*
     * Critical for RT systems:
     *
     * if a low-priority task owns this mutex and a higher-priority task
     * waits for it, the owner temporarily inherits the higher priority.
     */
    rc = pthread_mutexattr_setprotocol(
        &attr,
        PTHREAD_PRIO_INHERIT);

    if (rc != 0) {

        (void)pthread_mutexattr_destroy(&attr);

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        return AOS_ERR_CREATION_FAILED;
    }

    /*
     * Deliberately non-recursive.
     *
     * ERRORCHECK makes accidental double-locks and wrong-owner unlocks
     * detectable instead of silently turning them into undefined behavior.
     */
    rc = pthread_mutexattr_settype(
        &attr,
        PTHREAD_MUTEX_ERRORCHECK);

    if (rc != 0) {

        (void)pthread_mutexattr_destroy(&attr);

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        return AOS_ERR_CREATION_FAILED;
    }


    rc = pthread_mutex_init(
        &slot->native_mutex,
        &attr);

    (void)pthread_mutexattr_destroy(&attr);


    if (rc != 0) {

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        return AOS_ERR_CREATION_FAILED;
    }

    /*
     * Increment generation.
     *
     * Avoid zero simply to make diagnostics easier.
     */
    next_serial =
        (uint8_t)(slot->serial + 1u);

    if (next_serial == 0u) {
        next_serial = 1u;
    }

    slot->serial = next_serial;

    slot->id =
        AOS_IdPack(
            AOS_TYPE_MUTEX,
            slot->serial,
            index);

    slot->creator = creator;

    slot->ref_count = 0u;

    slot->in_use = true;

    memcpy(
        slot->name,
        name,
        name_length + 1u);

    *mutex_id = slot->id;

    pthread_mutex_unlock(
        &g_mutex_table_lock);

    return AOS_SUCCESS;
}

int32_t AOS_MutexLock(
    aos_id_t mutex_id)
{
    aos_posix_mutex_slot_t *slot;

    int32_t status;

    int rc;

    status =
        AOS_PosixMutexAcquireReference(
            mutex_id,
            &slot);

    if (status != AOS_SUCCESS) {
        return status;
    }

    rc = pthread_mutex_lock(
        &slot->native_mutex);


    if (rc == 0) {
        /*
         * Keep reference while this thread owns the mutex.
         */
        return AOS_SUCCESS;
    }

    AOS_PosixMutexReleaseReference(slot);

    if (rc == EDEADLK) {
        return AOS_ERR_INVALID_STATE;
    }

    return AOS_ERR_GENERIC;
}

int32_t AOS_MutexTryLock(
    aos_id_t mutex_id)
{
    aos_posix_mutex_slot_t *slot;

    int32_t status;

    int rc;

    status =
        AOS_PosixMutexAcquireReference(
            mutex_id,
            &slot);

    if (status != AOS_SUCCESS) {
        return status;
    }

    rc = pthread_mutex_trylock(
        &slot->native_mutex);

    if (rc == 0) {

        /*
         * Reference remains until Unlock().
         */
        return AOS_SUCCESS;
    }

    AOS_PosixMutexReleaseReference(slot);

    if (rc == EBUSY) {
        return AOS_ERR_BUSY;
    }

    if (rc == EDEADLK) {
        return AOS_ERR_INVALID_STATE;
    }

    return AOS_ERR_GENERIC;
}

int32_t AOS_MutexTimedLock(
    aos_id_t mutex_id,
    uint32_t timeout_ms)
{
    aos_posix_mutex_slot_t *slot;

    struct timespec deadline;

    int32_t status;

    uint64_t additional_ns;

    int rc;

    status =
        AOS_PosixMutexAcquireReference(
            mutex_id,
            &slot);

    if (status != AOS_SUCCESS) {
        return status;
    }

    if (clock_gettime(
            CLOCK_REALTIME,
            &deadline) != 0) {

        AOS_PosixMutexReleaseReference(slot);

        return AOS_ERR_GENERIC;
    }

    deadline.tv_sec +=
        (time_t)(timeout_ms / 1000u);

    additional_ns =
        (uint64_t)(timeout_ms % 1000u) *
        1000000ull;

    additional_ns +=
        (uint64_t)deadline.tv_nsec;

    deadline.tv_sec +=
        (time_t)(
            additional_ns /
            1000000000ull);

    deadline.tv_nsec =
        (long)(
            additional_ns %
            1000000000ull);

    rc = pthread_mutex_timedlock(
        &slot->native_mutex,
        &deadline);

    if (rc == 0) {

        /*
         * Keep reference until Unlock().
         */
        return AOS_SUCCESS;
    }

    AOS_PosixMutexReleaseReference(slot);

    if (rc == ETIMEDOUT) {
        return AOS_ERR_TIMEOUT;
    }

    if (rc == EDEADLK) {
        return AOS_ERR_INVALID_STATE;
    }

    return AOS_ERR_GENERIC;
}

int32_t AOS_MutexUnlock(
    aos_id_t mutex_id)
{
    aos_posix_mutex_slot_t *slot;

    int32_t status;

    int rc;

    pthread_mutex_lock(
        &g_mutex_table_lock);

    status =
        AOS_PosixValidateMutexIdLocked(
            mutex_id,
            &slot);

    pthread_mutex_unlock(
        &g_mutex_table_lock);

    if (status != AOS_SUCCESS) {
        return status;
    }

    rc = pthread_mutex_unlock(
        &slot->native_mutex);

    if (rc != 0) {

        if (rc == EPERM) {
            return AOS_ERR_INVALID_STATE;
        }

        return AOS_ERR_GENERIC;
    }

    /*
     * Successful unlock ends the ownership reference
     * created by Lock/TryLock/TimedLock.
     */
    AOS_PosixMutexReleaseReference(slot);

    return AOS_SUCCESS;
}

int32_t AOS_MutexDelete(
    aos_id_t mutex_id)
{
    aos_posix_mutex_slot_t *slot;

    int32_t status;

    int rc;

    pthread_mutex_lock(
        &g_mutex_table_lock);

    status =
        AOS_PosixValidateMutexIdLocked(
            mutex_id,
            &slot);

    if (status != AOS_SUCCESS) {

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        return status;
    }

    /*
     * Somebody owns or is waiting on the mutex.
     */
    if (slot->ref_count != 0u) {

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        return AOS_ERR_BUSY;
    }

    /*
     * Prevent new users from acquiring references while
     * pthread_mutex_destroy() is in progress.
     */
    slot->in_use = false;

    pthread_mutex_unlock(
        &g_mutex_table_lock);

    rc = pthread_mutex_destroy(
        &slot->native_mutex);

    if (rc != 0) {

        pthread_mutex_lock(
            &g_mutex_table_lock);

        slot->in_use = true;

        pthread_mutex_unlock(
            &g_mutex_table_lock);

        if (rc == EBUSY) {
            return AOS_ERR_BUSY;
        }

        return AOS_ERR_GENERIC;
    }

    /*
     * Do NOT clear serial.
     *
     * It must survive reuse so stale IDs remain detectable.
     */
    pthread_mutex_lock(
        &g_mutex_table_lock);

    slot->id = AOS_ID_NONE;
    slot->creator = AOS_ID_NONE;

    slot->ref_count = 0u;

    slot->name[0] = '\0';

    pthread_mutex_unlock(
        &g_mutex_table_lock);

    return AOS_SUCCESS;
}

int32_t AOS_MutexGetInfo(
    aos_id_t mutex_id,
    aos_mutex_info_t *info)
{
    aos_posix_mutex_slot_t *slot;

    int32_t status;

    if (info == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    pthread_mutex_lock(
        &g_mutex_table_lock);

    status =
        AOS_PosixValidateMutexIdLocked(mutex_id, &slot);

    if (status == AOS_SUCCESS) {

        memcpy(info->name, slot->name, AOS_MAX_NAME);

        info->creator = slot->creator;
    }

    pthread_mutex_unlock( &g_mutex_table_lock);

    return status;
}

