#include "aos_sem.h"
#include "aos_task.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct
{
    sem_t native_sem;

    /*
     * Fast-path fields. AOS_SemPost() reads/writes these WITHOUT the table
     * lock so it can be called from interrupt/signal context.
     *
     *   id:    AOS_ID_NONE while the slot is free; else the packed id
     *          this generation of the slot was created with. A single
     *          atomic compare against the caller's id both validates the
     *          type/index/serial (they're all encoded in the same 32 bits)
     *          and confirms the slot is still live.
     *
     *   state: 0 == empty, 1 == full. The compare-and-swap on this field
     *          is what enforces strict binary semantics: a Post only
     *          proceeds to sem_post() if it can flip 0 -> 1 itself.
     */
    _Atomic aos_id_t id;
    _Atomic uint8_t  state;

    aos_id_t creator;
    uint32_t ref_count;
    uint8_t  serial;
    bool     in_use;
    char     name[AOS_MAX_NAME];

} aos_posix_sem_slot_t;


static aos_posix_sem_slot_t
    g_sem_pool[AOS_MAX_SEMS];


/*
 * Internal mutex protecting the object table itself (slow-path fields
 * only: in_use, creator, ref_count, serial, name).
 *
 * AOS_SemPost() never takes this lock -- see the slot layout comment
 * above. Every other entry point does, and critical sections here must
 * stay very short, exactly like the mutex table lock.
 */
static pthread_mutex_t g_sem_table_lock = PTHREAD_MUTEX_INITIALIZER;

static bool g_sem_initialized = false;

int32_t AOS_PosixBinSemInit(void)
{
    uint16_t index;

    if (g_sem_initialized) {
        return AOS_SUCCESS;
    }

    memset(g_sem_pool, 0, sizeof(g_sem_pool));

    for (index = 0u;
         index < AOS_MAX_SEMS;
         ++index) {

        atomic_init(&g_sem_pool[index].id, AOS_ID_NONE);
        atomic_init(&g_sem_pool[index].state, (uint8_t)0u);
    }

    g_sem_initialized = true;

    return AOS_SUCCESS;
}

static int32_t AOS_PosixValidateSemIdLocked(
    aos_id_t sem_id,
    aos_posix_sem_slot_t **slot_out)
{
    uint16_t index;
    aos_posix_sem_slot_t *slot;
    aos_id_t slot_id;

    if (slot_out == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (AOS_IdType(sem_id) != AOS_TYPE_BINSEM) {
        return AOS_ERR_INVALID_ID;
    }

    index = AOS_IdIndex(sem_id);

    if (index >= AOS_MAX_SEMS) {
        return AOS_ERR_INVALID_ID;
    }

    slot = &g_sem_pool[index];

    if (!slot->in_use) {
        return AOS_ERR_INVALID_ID;
    }

    slot_id = atomic_load_explicit(&slot->id, memory_order_relaxed);

    if (slot_id != sem_id) {
        return AOS_ERR_INVALID_ID;
    }

    if (slot->serial != AOS_IdSerial(sem_id)) {
        return AOS_ERR_INVALID_ID;
    }

    *slot_out = slot;

    return AOS_SUCCESS;
}

static int32_t AOS_PosixSemAcquireReference(
    aos_id_t sem_id,
    aos_posix_sem_slot_t **slot_out)
{
    aos_posix_sem_slot_t *slot;
    int32_t status;

    pthread_mutex_lock(&g_sem_table_lock);

    status =
        AOS_PosixValidateSemIdLocked(
            sem_id,
            &slot);

    if (status == AOS_SUCCESS) {

        ++slot->ref_count;

        *slot_out = slot;
    }

    pthread_mutex_unlock(&g_sem_table_lock);

    return status;
}

static void AOS_PosixSemReleaseReference(
    aos_posix_sem_slot_t *slot)
{
    pthread_mutex_lock(&g_sem_table_lock);

    if (slot->ref_count > 0u) {
        --slot->ref_count;
    }

    pthread_mutex_unlock(&g_sem_table_lock);
}


int32_t AOS_SemCreate(
    aos_sem_t *sem_id,
    const char *name,
    aos_sem_state_t initial_state,
    uint32_t flags)
{
    aos_posix_sem_slot_t *slot = NULL;

    aos_task_t creator = AOS_TASK_NONE;

    aos_id_t new_id;

    size_t name_length;

    uint16_t index;

    uint8_t next_serial;

    unsigned int sem_value;


    if (sem_id == NULL || name == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (flags != AOS_SEM_FLAG_NONE) {
        return AOS_ERR_INVALID_PARAM;
    }

    if (initial_state != AOS_SEM_EMPTY &&
        initial_state != AOS_SEM_FULL) {
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


    pthread_mutex_lock(&g_sem_table_lock);

    /*
     * Enforce unique names.
     */
    for (index = 0u;
         index < AOS_MAX_SEMS;
         ++index) {

        if (g_sem_pool[index].in_use &&
            strcmp(g_sem_pool[index].name, name) == 0) {

            pthread_mutex_unlock(
                &g_sem_table_lock);

            return AOS_ERR_NAME_TAKEN;
        }
    }

    /*
     * Find a free static object slot.
     */
    for (index = 0u;
         index < AOS_MAX_SEMS;
         ++index) {

        if (!g_sem_pool[index].in_use) {
            slot = &g_sem_pool[index];
            break;
        }
    }

    if (slot == NULL) {

        pthread_mutex_unlock(
            &g_sem_table_lock);

        return AOS_ERR_NO_FREE_IDS;
    }

    sem_value = (initial_state == AOS_SEM_FULL) ? 1u : 0u;

    if (sem_init(&slot->native_sem, 0, sem_value) != 0) {

        pthread_mutex_unlock(
            &g_sem_table_lock);

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

    new_id =
        AOS_IdPack(
            AOS_TYPE_BINSEM,
            slot->serial,
            index);

    slot->creator = creator.id;

    slot->ref_count = 0u;

    slot->in_use = true;

    memcpy(
        slot->name,
        name,
        name_length + 1u);

    /*
     * state must be visible before id is published, so a lock-free
     * AOS_SemPost() that observes the new id never sees a stale state.
     */
    atomic_store_explicit(
        &slot->state,
        (uint8_t)sem_value,
        memory_order_relaxed);

    atomic_store_explicit(
        &slot->id,
        new_id,
        memory_order_release);

    sem_id->id = new_id;

    pthread_mutex_unlock(
        &g_sem_table_lock);

    return AOS_SUCCESS;
}

int32_t AOS_SemWait(
    aos_sem_t sem_id)
{
    aos_posix_sem_slot_t *slot;

    int32_t status;

    int rc;

    status =
        AOS_PosixSemAcquireReference(
            sem_id.id,
            &slot);

    if (status != AOS_SUCCESS) {
        return status;
    }

    do {
        rc = sem_wait(&slot->native_sem);
    } while (rc != 0 && errno == EINTR);

    if (rc == 0) {

        /*
         * Consumed while still holding the reference, so a concurrent
         * Delete() cannot destroy the slot out from under this store.
         */
        atomic_store_explicit(
            &slot->state,
            (uint8_t)0u,
            memory_order_release);

        AOS_PosixSemReleaseReference(slot);

        return AOS_SUCCESS;
    }

    AOS_PosixSemReleaseReference(slot);

    return AOS_ERR_GENERIC;
}

int32_t AOS_SemTryWait(
    aos_sem_t sem_id)
{
    aos_posix_sem_slot_t *slot;

    int32_t status;

    int rc;
    int saved_errno = 0;

    status =
        AOS_PosixSemAcquireReference(
            sem_id.id,
            &slot);

    if (status != AOS_SUCCESS) {
        return status;
    }

    do {
        rc = sem_trywait(&slot->native_sem);

        if (rc != 0) {
            saved_errno = errno;
        }
    } while (rc != 0 && saved_errno == EINTR);

    if (rc == 0) {

        atomic_store_explicit(
            &slot->state,
            (uint8_t)0u,
            memory_order_release);

        AOS_PosixSemReleaseReference(slot);

        return AOS_SUCCESS;
    }

    AOS_PosixSemReleaseReference(slot);

    if (saved_errno == EAGAIN) {
        return AOS_ERR_BUSY;
    }

    return AOS_ERR_GENERIC;
}

int32_t AOS_SemTimedWait(
    aos_sem_t sem_id,
    uint32_t timeout_ms)
{
    aos_posix_sem_slot_t *slot;

    struct timespec deadline;

    int32_t status;

    uint64_t additional_ns;

    int rc;
    int saved_errno = 0;

    status =
        AOS_PosixSemAcquireReference(
            sem_id.id,
            &slot);

    if (status != AOS_SUCCESS) {
        return status;
    }

    if (clock_gettime(
            CLOCK_REALTIME,
            &deadline) != 0) {

        AOS_PosixSemReleaseReference(slot);

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

    do {
        rc = sem_timedwait(&slot->native_sem, &deadline);

        if (rc != 0) {
            saved_errno = errno;
        }
    } while (rc != 0 && saved_errno == EINTR);

    if (rc == 0) {

        atomic_store_explicit(
            &slot->state,
            (uint8_t)0u,
            memory_order_release);

        AOS_PosixSemReleaseReference(slot);

        return AOS_SUCCESS;
    }

    AOS_PosixSemReleaseReference(slot);

    if (saved_errno == ETIMEDOUT) {
        return AOS_ERR_TIMEOUT;
    }

    return AOS_ERR_GENERIC;
}

int32_t AOS_SemPost(
    aos_sem_t sem_id)
{
    uint16_t index;

    aos_posix_sem_slot_t *slot;

    aos_id_t raw_id = sem_id.id;

    aos_id_t observed_id;

    uint8_t expected;

    if (AOS_IdType(raw_id) != AOS_TYPE_BINSEM) {
        return AOS_ERR_INVALID_ID;
    }

    index = AOS_IdIndex(raw_id);

    if (index >= AOS_MAX_SEMS) {
        return AOS_ERR_INVALID_ID;
    }

    slot = &g_sem_pool[index];

    /*
     * Lock-free by design -- see the slot layout comment at the top of
     * this file. No table lock, no blocking calls: safe to call from
     * interrupt/signal context.
     */
    observed_id =
        atomic_load_explicit(
            &slot->id,
            memory_order_acquire);

    if (observed_id != raw_id) {
        return AOS_ERR_INVALID_ID;
    }

    expected = (uint8_t)0u;

    if (!atomic_compare_exchange_strong_explicit(
            &slot->state,
            &expected,
            (uint8_t)1u,
            memory_order_acq_rel,
            memory_order_acquire)) {

        /*
         * Already full: strict binary semantics treat a double-post as a
         * usage bug rather than silently absorbing it.
         */
        return AOS_ERR_INVALID_STATE;
    }

    if (sem_post(&slot->native_sem) != 0) {

        /*
         * Practically unreachable (EINVAL/EOVERFLOW): roll back the flag
         * so a legitimate future post is not permanently blocked.
         */
        atomic_store_explicit(
            &slot->state,
            (uint8_t)0u,
            memory_order_release);

        return AOS_ERR_GENERIC;
    }

    return AOS_SUCCESS;
}

int32_t AOS_SemDelete(
    aos_sem_t sem_id)
{
    aos_posix_sem_slot_t *slot;

    aos_id_t raw_id = sem_id.id;

    int32_t status;

    int rc;
    int saved_errno;

    pthread_mutex_lock(
        &g_sem_table_lock);

    status =
        AOS_PosixValidateSemIdLocked(
            raw_id,
            &slot);

    if (status != AOS_SUCCESS) {

        pthread_mutex_unlock(
            &g_sem_table_lock);

        return status;
    }

    /*
     * Somebody is blocked in Wait/TryWait/TimedWait.
     */
    if (slot->ref_count != 0u) {

        pthread_mutex_unlock(
            &g_sem_table_lock);

        return AOS_ERR_BUSY;
    }

    /*
     * Clear the fast-path id first: any AOS_SemPost() that arrives after
     * this point observes AOS_ID_NONE and safely bails with
     * AOS_ERR_INVALID_ID instead of racing sem_destroy() below. A post
     * already past that check when this store lands is the documented
     * caller-responsibility hazard (see AOS_SemPost/AOS_SemDelete docs).
     */
    atomic_store_explicit(
        &slot->id,
        AOS_ID_NONE,
        memory_order_release);

    slot->in_use = false;

    pthread_mutex_unlock(
        &g_sem_table_lock);

    rc = sem_destroy(&slot->native_sem);
    saved_errno = errno;

    if (rc != 0) {

        pthread_mutex_lock(
            &g_sem_table_lock);

        slot->in_use = true;

        atomic_store_explicit(
            &slot->id,
            raw_id,
            memory_order_release);

        pthread_mutex_unlock(
            &g_sem_table_lock);

        if (saved_errno == EBUSY) {
            return AOS_ERR_BUSY;
        }

        return AOS_ERR_GENERIC;
    }

    /*
     * Do NOT clear serial.
     *
     * It must survive reuse so stale ids remain detectable.
     */
    pthread_mutex_lock(
        &g_sem_table_lock);

    slot->creator = AOS_ID_NONE;

    slot->ref_count = 0u;

    slot->name[0] = '\0';

    pthread_mutex_unlock(
        &g_sem_table_lock);

    return AOS_SUCCESS;
}

int32_t AOS_SemGetInfo(
    aos_sem_t sem_id,
    aos_sem_info_t *info)
{
    aos_posix_sem_slot_t *slot;

    int32_t status;

    if (info == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    pthread_mutex_lock(
        &g_sem_table_lock);

    status =
        AOS_PosixValidateSemIdLocked(sem_id.id, &slot);

    if (status == AOS_SUCCESS) {

        memcpy(info->name, slot->name, AOS_MAX_NAME);

        info->creator = (aos_task_t){ .id = slot->creator };
    }

    pthread_mutex_unlock(&g_sem_table_lock);

    return status;
}
