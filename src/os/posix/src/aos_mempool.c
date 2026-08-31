#include "aos_mempool.h"
#include "aos_task.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * A free block's first sizeof(uint32_t) bytes double as this node while
 * (and only while) the block sits on the free list. Once handed out via
 * AOS_MempoolAlloc(), the caller owns every byte of the block; this
 * layout is never read again until the block comes back via
 * AOS_MempoolFree().
 */
typedef struct
{
    _Atomic uint32_t next_index;
} aos_posix_mempool_free_node_t;

/*
 * The free-list head is a block INDEX, not a raw pointer -- packed
 * together with a generation counter into one atomic 64-bit word
 * (upper 32 bits = generation, lower 32 bits = index) so a single
 * native compare-and-swap closes the classic ABA hole in a lock-free
 * free list: thread 1 reads head=A, next=B, then stalls before its CAS;
 * thread 2 pops A, pops B, then frees A back onto the head. Without a
 * tag, thread 1's stale CAS would see head==A again and succeed,
 * setting head=B even though B is still in use elsewhere -- corrupting
 * the list. With the generation counter incrementing on every pop/push,
 * the packed word differs even when the index alone cycles back, so the
 * stale CAS correctly fails and retries against fresh data.
 *
 * A 64-bit CAS is natively lock-free on every target this project
 * actually cares about (x86-64 CMPXCHG, ARMv7-M+/AArch64 LDREXD/STREXD
 * or LDXR/STXR) without needing a 128-bit tagged-pointer CAS, which
 * generally isn't available on Cortex-M.
 *
 * AOS_MEMPOOL_NIL_INDEX (all-ones) marks "list is empty." The
 * generation counter can in principle wrap after ~4 billion pop/push
 * operations landing exactly during one stalled thread's race window --
 * accepted as negligible, the same tradeoff every production
 * tagged-pointer free list makes.
 */
typedef uint64_t aos_mempool_head_t;

#define AOS_MEMPOOL_NIL_INDEX ((uint32_t)0xFFFFFFFFu)

static inline aos_mempool_head_t AOS_MempoolPackHead(uint32_t generation, uint32_t index)
{
    return (((uint64_t)generation) << 32) | (uint64_t)index;
}
static inline uint32_t AOS_MempoolHeadIndex(aos_mempool_head_t head)
{
    return (uint32_t)(head & 0xFFFFFFFFu);
}
static inline uint32_t AOS_MempoolHeadGeneration(aos_mempool_head_t head)
{
    return (uint32_t)(head >> 32);
}

typedef struct
{
    void   *storage;
    size_t  block_size;
    size_t  block_count;

    /*
     * Fast-path fields. AOS_MempoolAlloc()/AOS_MempoolFree() read/write
     * these WITHOUT the table lock so they can be called from
     * interrupt/signal context -- same design as aos_sem.c's AOS_SemPost.
     *
     *   id:          AOS_ID_NONE while the slot is free; else the packed
     *                id this generation of the slot was created with.
     *   free_head:   Treiber-stack free list head, as a tagged index
     *                (see aos_mempool_head_t above).
     *   blocks_free: live free count, for diagnostics and the Delete()
     *                busy-check. Not load-bearing for correctness of
     *                Alloc/Free themselves (free_head alone is), just a
     *                convenience counter kept in lockstep with it.
     */
    _Atomic aos_id_t id;
    _Atomic aos_mempool_head_t free_head;
    _Atomic size_t blocks_free;

    aos_id_t creator;
    uint8_t  serial;
    bool     in_use;
    char     name[AOS_MAX_NAME];

} aos_posix_mempool_slot_t;


static aos_posix_mempool_slot_t
    g_mempool_pool[AOS_MAX_MEMPOOLS];


/*
 * Internal mutex protecting the object table itself (slow-path fields
 * only: in_use, creator, serial, name, storage/block_size/block_count).
 *
 * AOS_MempoolAlloc()/AOS_MempoolFree() never take this lock -- see the
 * slot layout comment above. Every other entry point does, and critical
 * sections here must stay very short, exactly like the mutex/sem table
 * locks.
 */
static pthread_mutex_t g_mempool_table_lock = PTHREAD_MUTEX_INITIALIZER;

static bool g_mempool_initialized = false;

int32_t AOS_PosixMempoolInit(void)
{
    uint16_t index;

    if (g_mempool_initialized) {
        return AOS_SUCCESS;
    }

    memset(g_mempool_pool, 0, sizeof(g_mempool_pool));

    for (index = 0u;
         index < AOS_MAX_MEMPOOLS;
         ++index) {

        atomic_init(&g_mempool_pool[index].id, AOS_ID_NONE);
        atomic_init(&g_mempool_pool[index].free_head, AOS_MempoolPackHead(0u, AOS_MEMPOOL_NIL_INDEX));
        atomic_init(&g_mempool_pool[index].blocks_free, (size_t)0u);
    }

    g_mempool_initialized = true;

    return AOS_SUCCESS;
}

static int32_t AOS_PosixValidateMempoolIdLocked(
    aos_id_t pool_id,
    aos_posix_mempool_slot_t **slot_out)
{
    uint16_t index;
    aos_posix_mempool_slot_t *slot;
    aos_id_t slot_id;

    if (slot_out == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (AOS_IdType(pool_id) != AOS_TYPE_MEMPOOL) {
        return AOS_ERR_INVALID_ID;
    }

    index = AOS_IdIndex(pool_id);

    if (index >= AOS_MAX_MEMPOOLS) {
        return AOS_ERR_INVALID_ID;
    }

    slot = &g_mempool_pool[index];

    if (!slot->in_use) {
        return AOS_ERR_INVALID_ID;
    }

    slot_id = atomic_load_explicit(&slot->id, memory_order_relaxed);

    if (slot_id != pool_id) {
        return AOS_ERR_INVALID_ID;
    }

    if (slot->serial != AOS_IdSerial(pool_id)) {
        return AOS_ERR_INVALID_ID;
    }

    *slot_out = slot;

    return AOS_SUCCESS;
}


int32_t AOS_MempoolCreate(
    aos_mempool_t *pool_id,
    const char *name,
    void *storage,
    size_t block_size,
    size_t block_count,
    uint32_t flags)
{
    aos_posix_mempool_slot_t *slot = NULL;

    aos_task_t creator = AOS_TASK_NONE;

    aos_id_t new_id;

    size_t name_length;

    uint16_t index;

    uint8_t next_serial;

    size_t i;

    uint8_t *bytes;

    uint32_t prev_index;


    if (pool_id == NULL || name == NULL || storage == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (flags != AOS_MEMPOOL_FLAG_NONE) {
        return AOS_ERR_INVALID_PARAM;
    }

    /*
     * The free-list bookkeeping itself only needs sizeof(uint32_t) bytes
     * per block now (see aos_mempool_head_t above), but the documented
     * contract stays pointer-size/alignment regardless: mempool hands
     * blocks back for the CALLER to store arbitrary payloads in
     * (structs containing pointers or doubles, say), so it must keep
     * guaranteeing pointer alignment, not just what its own bookkeeping
     * happens to require today.
     */
    if (block_size < sizeof(void *)) {
        return AOS_ERR_INVALID_SIZE;
    }

    if (block_count == 0u) {
        return AOS_ERR_INVALID_SIZE;
    }

    if (block_count > (size_t)AOS_MEMPOOL_NIL_INDEX) {
        return AOS_ERR_INVALID_SIZE;
    }

    /*
     * Every block's start address must stay pointer-aligned. storage
     * aligned + block_size a multiple of sizeof(void*) together
     * guarantee that for every block, not just the first.
     */
    if (((uintptr_t)storage % sizeof(void *)) != 0u) {
        return AOS_ERR_INVALID_PARAM;
    }

    if ((block_size % sizeof(void *)) != 0u) {
        return AOS_ERR_INVALID_PARAM;
    }

    name_length = strlen(name);

    if (name_length == 0u) {
        return AOS_ERR_INVALID_PARAM;
    }

    if (name_length >= AOS_MAX_NAME) {
        return AOS_ERR_NAME_TOO_LONG;
    }

    (void)AOS_TaskGetId(&creator);


    pthread_mutex_lock(&g_mempool_table_lock);

    /*
     * Enforce unique names.
     */
    for (index = 0u;
         index < AOS_MAX_MEMPOOLS;
         ++index) {

        if (g_mempool_pool[index].in_use &&
            strcmp(g_mempool_pool[index].name, name) == 0) {

            pthread_mutex_unlock(
                &g_mempool_table_lock);

            return AOS_ERR_NAME_TAKEN;
        }
    }

    /*
     * Find a free static object slot.
     */
    for (index = 0u;
         index < AOS_MAX_MEMPOOLS;
         ++index) {

        if (!g_mempool_pool[index].in_use) {
            slot = &g_mempool_pool[index];
            break;
        }
    }

    if (slot == NULL) {

        pthread_mutex_unlock(
            &g_mempool_table_lock);

        return AOS_ERR_NO_FREE_IDS;
    }

    /*
     * Thread the free list through `storage`, by index rather than raw
     * pointer (see aos_mempool_head_t above). Order doesn't matter --
     * blocks are interchangeable -- so simply link each block to the
     * previous one built.
     */
    bytes = (uint8_t *)storage;
    prev_index = AOS_MEMPOOL_NIL_INDEX;

    for (i = 0u; i < block_count; ++i) {

        aos_posix_mempool_free_node_t *node =
            (aos_posix_mempool_free_node_t *)(void *)(bytes + (i * block_size));

        atomic_init(&node->next_index, prev_index);

        prev_index = (uint32_t)i;
    }

    slot->storage = storage;
    slot->block_size = block_size;
    slot->block_count = block_count;

    atomic_init(&slot->free_head, AOS_MempoolPackHead(0u, prev_index));
    atomic_init(&slot->blocks_free, block_count);

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
            AOS_TYPE_MEMPOOL,
            slot->serial,
            index);

    slot->creator = creator.id;

    slot->in_use = true;

    memcpy(
        slot->name,
        name,
        name_length + 1u);

    /*
     * Everything else must be visible before id is published, so a
     * lock-free AOS_MempoolAlloc()/Free() that observes the new id never
     * sees a stale free list or count.
     */
    atomic_store_explicit(
        &slot->id,
        new_id,
        memory_order_release);

    pool_id->id = new_id;

    pthread_mutex_unlock(
        &g_mempool_table_lock);

    return AOS_SUCCESS;
}

int32_t AOS_MempoolAlloc(
    aos_mempool_t pool_id,
    void **block_out)
{
    uint16_t index;

    aos_posix_mempool_slot_t *slot;

    aos_id_t raw_id = pool_id.id;

    aos_id_t observed_id;

    aos_mempool_head_t old_head;
    aos_mempool_head_t new_head;

    uint32_t block_index;
    uint32_t next_index;

    if (block_out == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (AOS_IdType(raw_id) != AOS_TYPE_MEMPOOL) {
        return AOS_ERR_INVALID_ID;
    }

    index = AOS_IdIndex(raw_id);

    if (index >= AOS_MAX_MEMPOOLS) {
        return AOS_ERR_INVALID_ID;
    }

    slot = &g_mempool_pool[index];

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

    old_head =
        atomic_load_explicit(
            &slot->free_head,
            memory_order_acquire);

    for (;;) {

        block_index = AOS_MempoolHeadIndex(old_head);

        if (block_index == AOS_MEMPOOL_NIL_INDEX) {
            return AOS_ERR_BUSY;
        }

        {
            aos_posix_mempool_free_node_t *node =
                (aos_posix_mempool_free_node_t *)(void *)
                    ((uint8_t *)slot->storage + ((size_t)block_index * slot->block_size));

            next_index =
                atomic_load_explicit(
                    &node->next_index,
                    memory_order_relaxed);
        }

        new_head =
            AOS_MempoolPackHead(
                AOS_MempoolHeadGeneration(old_head) + 1u,
                next_index);

        if (atomic_compare_exchange_weak_explicit(
                &slot->free_head,
                &old_head,
                new_head,
                memory_order_acq_rel,
                memory_order_acquire)) {
            break;
        }

        /*
         * CAS failed: `old_head` was updated in place to the current
         * value. Loop and retry against it.
         */
    }

    atomic_fetch_sub_explicit(
        &slot->blocks_free,
        (size_t)1u,
        memory_order_relaxed);

    *block_out = (uint8_t *)slot->storage + ((size_t)block_index * slot->block_size);

    return AOS_SUCCESS;
}

int32_t AOS_MempoolFree(
    aos_mempool_t pool_id,
    void *block)
{
    uint16_t index;

    aos_posix_mempool_slot_t *slot;

    aos_id_t raw_id = pool_id.id;

    aos_id_t observed_id;

    aos_posix_mempool_free_node_t *node;

    aos_mempool_head_t old_head;
    aos_mempool_head_t new_head;

    uint32_t block_index;

    if (block == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    if (AOS_IdType(raw_id) != AOS_TYPE_MEMPOOL) {
        return AOS_ERR_INVALID_ID;
    }

    index = AOS_IdIndex(raw_id);

    if (index >= AOS_MAX_MEMPOOLS) {
        return AOS_ERR_INVALID_ID;
    }

    slot = &g_mempool_pool[index];

    observed_id =
        atomic_load_explicit(
            &slot->id,
            memory_order_acquire);

    if (observed_id != raw_id) {
        return AOS_ERR_INVALID_ID;
    }

    node = (aos_posix_mempool_free_node_t *)block;

    block_index =
        (uint32_t)(
            ((uint8_t *)block - (uint8_t *)slot->storage) /
            slot->block_size);

    old_head =
        atomic_load_explicit(
            &slot->free_head,
            memory_order_acquire);

    for (;;) {

        atomic_store_explicit(
            &node->next_index,
            AOS_MempoolHeadIndex(old_head),
            memory_order_relaxed);

        new_head =
            AOS_MempoolPackHead(
                AOS_MempoolHeadGeneration(old_head) + 1u,
                block_index);

        if (atomic_compare_exchange_weak_explicit(
                &slot->free_head,
                &old_head,
                new_head,
                memory_order_acq_rel,
                memory_order_acquire)) {
            break;
        }
    }

    atomic_fetch_add_explicit(
        &slot->blocks_free,
        (size_t)1u,
        memory_order_relaxed);

    return AOS_SUCCESS;
}

int32_t AOS_MempoolDelete(
    aos_mempool_t pool_id)
{
    aos_posix_mempool_slot_t *slot;

    int32_t status;

    size_t free_now;


    pthread_mutex_lock(
        &g_mempool_table_lock);

    status =
        AOS_PosixValidateMempoolIdLocked(
            pool_id.id,
            &slot);

    if (status != AOS_SUCCESS) {

        pthread_mutex_unlock(
            &g_mempool_table_lock);

        return status;
    }

    free_now =
        atomic_load_explicit(
            &slot->blocks_free,
            memory_order_acquire);

    /*
     * Somebody still holds a block.
     */
    if (free_now != slot->block_count) {

        pthread_mutex_unlock(
            &g_mempool_table_lock);

        return AOS_ERR_BUSY;
    }

    /*
     * Clear the fast-path id first: any Alloc/Free that arrives after
     * this point observes AOS_ID_NONE and safely bails with
     * AOS_ERR_INVALID_ID instead of touching a pool being torn down. An
     * Alloc/Free already past that check when this store lands is the
     * documented caller-responsibility hazard (see AOS_MempoolAlloc/
     * AOS_MempoolDelete docs).
     */
    atomic_store_explicit(
        &slot->id,
        AOS_ID_NONE,
        memory_order_release);

    slot->in_use = false;

    pthread_mutex_unlock(
        &g_mempool_table_lock);

    /*
     * No native OS resource to tear down -- pure userspace bookkeeping
     * over caller-supplied storage that the OSAL never owned. Unlike
     * mutex/sem there's no possible failure here requiring rollback.
     */
    pthread_mutex_lock(
        &g_mempool_table_lock);

    slot->creator = AOS_ID_NONE;

    slot->storage = NULL;

    slot->name[0] = '\0';

    pthread_mutex_unlock(
        &g_mempool_table_lock);

    return AOS_SUCCESS;
}

int32_t AOS_MempoolGetInfo(
    aos_mempool_t pool_id,
    aos_mempool_info_t *info)
{
    aos_posix_mempool_slot_t *slot;

    int32_t status;

    if (info == NULL) {
        return AOS_ERR_INVALID_POINTER;
    }

    pthread_mutex_lock(
        &g_mempool_table_lock);

    status =
        AOS_PosixValidateMempoolIdLocked(pool_id.id, &slot);

    if (status == AOS_SUCCESS) {

        memcpy(info->name, slot->name, AOS_MAX_NAME);

        info->creator = (aos_task_t){ .id = slot->creator };

        info->block_size = slot->block_size;
        info->block_count = slot->block_count;

        info->blocks_free =
            atomic_load_explicit(
                &slot->blocks_free,
                memory_order_relaxed);
    }

    pthread_mutex_unlock(&g_mempool_table_lock);

    return status;
}
