#include "aos_osal.h"
#include "aos_task.h"
#include "aos_mempool.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
 * marker + padding so sizeof() is comfortably >= sizeof(void*) and an
 * exact multiple of it on both 32- and 64-bit -- AOS_MempoolCreate
 * requires both.
 */
typedef struct
{
    int marker;
    int _pad;
} basic_block_t;

static basic_block_t g_basic_storage[4];

/*
 * Deliberately misaligned/undersized storage for the validation tests
 * below: one byte short of pointer alignment.
 */
static uint8_t g_misaligned_storage[1 + sizeof(basic_block_t) * 4];


#define STRESS_POOL_BLOCKS 4u
#define STRESS_TASKS       6u
#define STRESS_ITERATIONS  2000

typedef struct
{
    int marker;
    int _pad;
} stress_block_t;

static stress_block_t g_stress_storage[STRESS_POOL_BLOCKS];

static aos_mempool_t g_stress_pool = AOS_MEMPOOL_NONE;

static volatile int32_t g_stress_result[STRESS_TASKS];
static volatile bool    g_stress_done[STRESS_TASKS];

static void stress_worker(void *arg)
{
    int idx = *(int *)arg;
    int i;
    int32_t status = AOS_SUCCESS;

    for (i = 0; i < STRESS_ITERATIONS; ++i)
    {
        void *block = NULL;
        int32_t rc;

        do
        {
            rc = AOS_MempoolAlloc(g_stress_pool, &block);

            if (rc == AOS_ERR_BUSY)
            {
                (void)AOS_TaskYield();
            }
        } while (rc == AOS_ERR_BUSY);

        if (rc != AOS_SUCCESS)
        {
            status = rc;
            break;
        }

        /*
         * Stamp the block with our own identity, yield to give any
         * concurrently-racing task a chance to run, then verify nothing
         * changed. If the lock-free free list ever handed the same
         * block to two tasks at once, this would very likely catch it.
         */
        ((stress_block_t *)block)->marker = idx;

        (void)AOS_TaskYield();

        if (((stress_block_t *)block)->marker != idx)
        {
            status = AOS_ERR_GENERIC;
            break;
        }

        rc = AOS_MempoolFree(g_stress_pool, block);

        if (rc != AOS_SUCCESS)
        {
            status = rc;
            break;
        }
    }

    g_stress_result[idx] = status;
    g_stress_done[idx] = true;
}

int main(void)
{
    aos_mempool_t pool_id = AOS_MEMPOOL_NONE;
    aos_mempool_t other_pool_id = AOS_MEMPOOL_NONE;

    aos_mempool_info_t info;

    void *blocks[4];

    int32_t status;

    unsigned int i;


    if (AOS_Init() != AOS_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize OSAL\n");
        return 1;
    }


    /*
     * TEST: parameter validation.
     */
    status = AOS_MempoolCreate(NULL, "bad", g_basic_storage, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_INVALID_POINTER) return 2;

    status = AOS_MempoolCreate(&pool_id, NULL, g_basic_storage, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_INVALID_POINTER) return 3;

    status = AOS_MempoolCreate(&pool_id, "bad", NULL, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_INVALID_POINTER) return 4;

    status = AOS_MempoolCreate(&pool_id, "bad-flags", g_basic_storage, sizeof(basic_block_t), 4u, 0xFFu);
    if (status != AOS_ERR_INVALID_PARAM) return 5;

    status = AOS_MempoolCreate(&pool_id, "too-small", g_basic_storage, sizeof(void *) - 1u, 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_INVALID_SIZE) return 6;

    status = AOS_MempoolCreate(&pool_id, "zero-count", g_basic_storage, sizeof(basic_block_t), 0u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_INVALID_SIZE) return 7;

    /* g_misaligned_storage + 1 is one byte off pointer alignment. */
    status = AOS_MempoolCreate(&pool_id, "unaligned-storage", g_misaligned_storage + 1, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_INVALID_PARAM) return 8;

    if (sizeof(void *) % 2u == 0u)
    {
        /* block_size not a multiple of sizeof(void*). */
        status = AOS_MempoolCreate(&pool_id, "unaligned-block", g_basic_storage, sizeof(void *) + 1u, 2u, AOS_MEMPOOL_FLAG_NONE);
        if (status != AOS_ERR_INVALID_PARAM) return 9;
    }

    status = AOS_MempoolCreate(&pool_id, "", g_basic_storage, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_INVALID_PARAM) return 10;

    status = AOS_MempoolCreate(&pool_id, "this-name-is-definitely-too-long-for-aos", g_basic_storage, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_NAME_TOO_LONG) return 11;

    printf("Parameter validation test: PASS\n");


    /*
     * Create the real test pool: 4 blocks.
     */
    status = AOS_MempoolCreate(&pool_id, "test-pool", g_basic_storage, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_SUCCESS)
    {
        fprintf(stderr, "AOS_MempoolCreate failed: %s\n", AOS_StrError(status));
        return 12;
    }

    if (AOS_IdType(pool_id.id) != AOS_TYPE_MEMPOOL) return 13;


    /*
     * TEST: duplicate name.
     */
    status = AOS_MempoolCreate(&other_pool_id, "test-pool", g_basic_storage, sizeof(basic_block_t), 4u, AOS_MEMPOOL_FLAG_NONE);
    if (status != AOS_ERR_NAME_TAKEN)
    {
        fprintf(stderr, "Duplicate name: expected AOS_ERR_NAME_TAKEN, got %s\n", AOS_StrError(status));
        return 14;
    }

    printf("Duplicate name test: PASS\n");


    /*
     * TEST: GetInfo before any allocation.
     */
    status = AOS_MempoolGetInfo(pool_id, &info);
    if (status != AOS_SUCCESS) return 15;
    if (info.block_size != sizeof(basic_block_t)) return 16;
    if (info.block_count != 4u) return 17;
    if (info.blocks_free != 4u) return 18;

    printf("GetInfo test: PASS\n");


    /*
     * TEST: allocate all 4 blocks, then exhaustion.
     */
    for (i = 0u; i < 4u; ++i)
    {
        status = AOS_MempoolAlloc(pool_id, &blocks[i]);
        if (status != AOS_SUCCESS)
        {
            fprintf(stderr, "Alloc %u failed: %s\n", i, AOS_StrError(status));
            return 19;
        }
    }

    {
        void *extra = NULL;

        status = AOS_MempoolAlloc(pool_id, &extra);
        if (status != AOS_ERR_BUSY)
        {
            fprintf(stderr, "Exhausted alloc: expected AOS_ERR_BUSY, got %s\n", AOS_StrError(status));
            return 20;
        }
    }

    printf("Exhaustion test: PASS\n");


    /*
     * TEST: allocated blocks are distinct addresses.
     */
    for (i = 0u; i < 4u; ++i)
    {
        unsigned int j;

        for (j = i + 1u; j < 4u; ++j)
        {
            if (blocks[i] == blocks[j])
            {
                fprintf(stderr, "Alloc handed out the same block twice\n");
                return 21;
            }
        }
    }


    /*
     * TEST: cannot delete a pool with outstanding blocks.
     */
    status = AOS_MempoolDelete(pool_id);
    if (status != AOS_ERR_BUSY)
    {
        fprintf(stderr, "Busy delete: expected AOS_ERR_BUSY, got %s\n", AOS_StrError(status));
        return 22;
    }

    printf("Busy delete test: PASS\n");


    /*
     * TEST: free them all back, GetInfo reflects it, then re-alloc works.
     */
    for (i = 0u; i < 4u; ++i)
    {
        status = AOS_MempoolFree(pool_id, blocks[i]);
        if (status != AOS_SUCCESS) return 23;
    }

    status = AOS_MempoolGetInfo(pool_id, &info);
    if (status != AOS_SUCCESS) return 24;
    if (info.blocks_free != 4u) return 25;

    {
        void *block = NULL;

        status = AOS_MempoolAlloc(pool_id, &block);
        if (status != AOS_SUCCESS) return 26;

        status = AOS_MempoolFree(pool_id, block);
        if (status != AOS_SUCCESS) return 27;
    }

    printf("Free/re-alloc test: PASS\n");


    /*
     * TEST: delete an idle pool.
     */
    status = AOS_MempoolDelete(pool_id);
    if (status != AOS_SUCCESS)
    {
        fprintf(stderr, "AOS_MempoolDelete failed: %s\n", AOS_StrError(status));
        return 28;
    }

    printf("Pool delete test: PASS\n");


    /*
     * TEST: stale id must be rejected by every entry point.
     */
    {
        void *block = NULL;

        if (AOS_MempoolAlloc(pool_id, &block) != AOS_ERR_INVALID_ID) return 29;
    }

    if (AOS_MempoolGetInfo(pool_id, &info) != AOS_ERR_INVALID_ID) return 30;

    printf("Stale ID test: PASS\n");


    /*
     * TEST: real multi-threaded contention over a small shared pool.
     * The strongest check that the lock-free free list is actually
     * correct, not just correct-looking.
     */
    status = AOS_MempoolCreate(
        &g_stress_pool,
        "stress-pool",
        g_stress_storage,
        sizeof(stress_block_t),
        STRESS_POOL_BLOCKS,
        AOS_MEMPOOL_FLAG_NONE);

    if (status != AOS_SUCCESS)
    {
        fprintf(stderr, "Stress pool create failed: %s\n", AOS_StrError(status));
        return 31;
    }

    {
        aos_task_t worker_ids[STRESS_TASKS];
        int worker_indices[STRESS_TASKS];
        char worker_name[AOS_MAX_NAME];

        for (i = 0u; i < STRESS_TASKS; ++i)
        {
            g_stress_result[i] = AOS_ERR_GENERIC;
            g_stress_done[i] = false;
            worker_indices[i] = (int)i;

            snprintf(worker_name, sizeof(worker_name), "mp-stress-%u", i);

            status = AOS_TaskCreate(
                &worker_ids[i],
                worker_name,
                stress_worker,
                &worker_indices[i],
                NULL,
                64u * 1024u,
                AOS_TASK_PRIORITY_DEFAULT,
                AOS_TASK_FLAG_NONE);

            if (status != AOS_SUCCESS)
            {
                fprintf(stderr, "Stress worker %u create failed: %s\n", i, AOS_StrError(status));
                return 32;
            }
        }

        for (i = 0u; i < STRESS_TASKS; ++i)
        {
            if (AOS_TaskJoin(worker_ids[i]) != AOS_SUCCESS) return 33;
            if (AOS_TaskDelete(worker_ids[i]) != AOS_SUCCESS) return 34;
        }
    }

    for (i = 0u; i < STRESS_TASKS; ++i)
    {
        if (!g_stress_done[i]) return 35;

        if (g_stress_result[i] != AOS_SUCCESS)
        {
            fprintf(stderr,
                    "Stress worker %u reported %s\n",
                    i, AOS_StrError(g_stress_result[i]));
            return 36;
        }
    }

    status = AOS_MempoolGetInfo(g_stress_pool, &info);
    if (status != AOS_SUCCESS) return 37;

    if (info.blocks_free != STRESS_POOL_BLOCKS)
    {
        fprintf(stderr,
                "Stress test left blocks_free=%zu, expected %u -- "
                "leaked or double-freed block\n",
                info.blocks_free, STRESS_POOL_BLOCKS);
        return 38;
    }

    if (AOS_MempoolDelete(g_stress_pool) != AOS_SUCCESS) return 39;

    printf("Concurrency stress test: PASS (%u tasks x %d iterations over %u blocks)\n",
           STRESS_TASKS, STRESS_ITERATIONS, STRESS_POOL_BLOCKS);


    puts("ALL MEMPOOL TESTS PASSED");

    return 0;
}
