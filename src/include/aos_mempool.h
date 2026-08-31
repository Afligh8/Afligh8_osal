#ifndef AOS_MEMPOOL_H
#define AOS_MEMPOOL_H

#include "aos_osal.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed-size block pool.
 *
 * A mempool never allocates memory itself -- the caller supplies the
 * backing storage (a static/global buffer, exactly like AOS_TaskCreate's
 * stack parameter) and the OSAL just carves it into block_count blocks
 * of block_size bytes each, managed as an intrusive free list threaded
 * through the free blocks themselves (zero per-block bookkeeping
 * overhead beyond block_size >= sizeof(void*)).
 *
 * Blocks within a pool are always the same size. Variable-size
 * allocation is deliberately out of scope: it reintroduces the
 * fragmentation and non-determinism this exists to avoid.
 */

#define AOS_MEMPOOL_FLAG_NONE 0u

typedef struct
{
    char       name[AOS_MAX_NAME];
    aos_task_t creator;
    size_t     block_size;
    size_t     block_count;
    size_t     blocks_free;   /* live count -- diagnostics/telemetry */
} aos_mempool_info_t;

/*
 * Create a fixed-size block pool over caller-supplied storage.
 *
 * storage:     caller-owned buffer, at least block_size * block_count
 *              bytes, pointer-aligned (any static/global array of a
 *              normally-aligned type already satisfies this). Never
 *              freed or reallocated by the OSAL -- it must outlive the
 *              pool.
 * block_size:  bytes per block. Must be >= sizeof(void*) and a multiple
 *              of sizeof(void*) (needed for the free-list linkage while
 *              a block is unallocated, and to keep every block's start
 *              address aligned).
 * block_count: number of blocks. Must be >= 1.
 *
 * ISR-safe: no.  blocking: no.
 */
int32_t AOS_MempoolCreate(
    aos_mempool_t *pool_id,
    const char *name,
    void *storage,
    size_t block_size,
    size_t block_count,
    uint32_t flags);

/*
 * Delete a pool.
 *
 * Fails with AOS_ERR_BUSY unless every block has been returned via
 * AOS_MempoolFree() first -- deleting while blocks are still held would
 * leave dangling pool ids that a later AOS_MempoolFree() could corrupt.
 *
 * The caller is responsible for ensuring no interrupt context can still
 * call AOS_MempoolAlloc()/AOS_MempoolFree() on this id once deletion
 * begins -- both are lock-free by design (see below) and cannot be held
 * off by this call, exactly like AOS_SemPost vs AOS_SemDelete.
 *
 * ISR-safe: no.  blocking: no.
 */
int32_t AOS_MempoolDelete(
    aos_mempool_t pool_id);

/*
 * Take one block from the pool.
 *
 * Never blocks -- an exhausted pool returns AOS_ERR_BUSY immediately.
 * A flight-control system generally doesn't want unbounded blocking
 * waits for memory; if a bounded wait variant turns out to be needed,
 * it can be added later without disturbing this one (mirrors how
 * AOS_MutexLock/TryLock/TimedLock coexist).
 *
 * block_out: out; receives the block pointer on success, untouched on
 *            failure. Block contents are uninitialized (not zeroed),
 *            matching malloc()'s convention rather than calloc()'s.
 *
 * ISR-safe: yes. Lock-free (atomic compare-and-swap on the free-list
 * head), same design and the same narrow caller-responsibility hazard
 * around concurrent Delete() as AOS_SemPost.  blocking: no.
 */
int32_t AOS_MempoolAlloc(
    aos_mempool_t pool_id,
    void **block_out);

/*
 * Return a block to the pool it came from.
 *
 * Undefined behaviour if block did not come from this pool's Alloc(),
 * or is freed twice -- mempool does not track which blocks are
 * currently allocated beyond the free-count used for diagnostics and
 * the Delete() busy-check, exactly as free()/malloc() don't either.
 *
 * ISR-safe: yes.  blocking: no.
 */
int32_t AOS_MempoolFree(
    aos_mempool_t pool_id,
    void *block);

/*
 * Obtain portable pool information, including a live blocks_free count.
 *
 * ISR-safe: no.  blocking: no.
 */
int32_t AOS_MempoolGetInfo(
    aos_mempool_t pool_id,
    aos_mempool_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* AOS_MEMPOOL_H */
