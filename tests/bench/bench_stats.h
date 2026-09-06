#ifndef BENCH_STATS_H
#define BENCH_STATS_H

/*
 * Shared measurement infrastructure for Phase 6 (AFlight8 Bench Manual).
 * Header-only on purpose -- every bench executable just #includes this,
 * no separate library target to wire into CMakeLists.txt.
 *
 * This is a STOPWATCH, not a benchmark. It doesn't know what you're
 * timing or when to warm up -- that's every exercise's own job. All it
 * does is: record a fixed-capacity series of aos_time_t samples, and
 * summarize them into the statistics a real-time latency claim actually
 * needs (see the Bench Manual, section 02) -- never just a mean.
 *
 * Fixed-capacity, no malloc/free: consistent with how the rest of this
 * OSAL avoids dynamic allocation on measurement-sensitive paths. Bump
 * BENCH_MAX_SAMPLES if an exercise genuinely needs more; don't make this
 * dynamic just for convenience.
 */

#include "aos_osal.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BENCH_MAX_SAMPLES 20000u

/*
 * sizeof(bench_series_t) is ~160KB at this capacity. Declare it
 * `static` or file-scope, NEVER as a local variable inside a task --
 * this project's tasks are typically created with a 64KB stack
 * (AOS_TaskCreate's stack_size argument), so a local instance of this
 * struct overflows on its own before you write a single sample.
 */
typedef struct
{
    aos_time_t samples[BENCH_MAX_SAMPLES];
    size_t     count;
    size_t     dropped;   /* samples that didn't fit -- see BenchSeriesAdd() */
} bench_series_t;

typedef struct
{
    size_t n;
    double min_us;
    double max_us;
    double mean_us;
    double median_us;
    double p95_us;
    double p99_us;
    double stddev_us;
} bench_summary_t;

static inline void BenchSeriesReset(bench_series_t *series)
{
    series->count = 0u;
    series->dropped = 0u;
}

/*
 * Add one sample, in microseconds (matching aos_time_t's own unit --
 * see AOS_TimeGet()'s doc comment). Returns false if the series is
 * already at BENCH_MAX_SAMPLES; the sample is counted in `dropped`
 * rather than silently discarded, so a benchmark that overflows this
 * buffer is visible in its own output, not just wrong.
 */
static inline bool BenchSeriesAdd(bench_series_t *series, aos_time_t sample_us)
{
    if (series->count >= BENCH_MAX_SAMPLES) {
        series->dropped++;
        return false;
    }

    series->samples[series->count] = sample_us;
    series->count++;

    return true;
}

static int BenchCompareTime(const void *a, const void *b)
{
    aos_time_t va = *(const aos_time_t *)a;
    aos_time_t vb = *(const aos_time_t *)b;

    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/*
 * Sorts a COPY of the series (the original is left in recorded order,
 * in case a caller wants that too -- e.g. to plot jitter over time,
 * not just its distribution). O(n log n); call this once after
 * recording, not inside a timed region.
 */
static inline void BenchSeriesSummarize(const bench_series_t *series, bench_summary_t *out)
{
    /* static, not a local -- same ~160KB stack hazard as bench_series_t
     * itself (see its comment). Also means this function is not
     * reentrant/thread-safe; it's a one-shot post-processing call, not
     * something to invoke concurrently on multiple series. */
    static aos_time_t sorted[BENCH_MAX_SAMPLES];
    size_t n = series->count;
    size_t i;
    double sum = 0.0;
    double mean;
    double sq_sum = 0.0;

    memset(out, 0, sizeof(*out));
    out->n = n;

    if (n == 0u) {
        return;
    }

    memcpy(sorted, series->samples, n * sizeof(aos_time_t));
    qsort(sorted, n, sizeof(aos_time_t), BenchCompareTime);

    for (i = 0u; i < n; ++i) {
        sum += (double)sorted[i];
    }
    mean = sum / (double)n;

    for (i = 0u; i < n; ++i) {
        double d = (double)sorted[i] - mean;
        sq_sum += d * d;
    }

    out->min_us    = (double)sorted[0];
    out->max_us    = (double)sorted[n - 1u];
    out->mean_us   = mean;
    out->median_us = (double)sorted[n / 2u];
    out->p95_us    = (double)sorted[(size_t)((double)(n - 1u) * 0.95)];
    out->p99_us    = (double)sorted[(size_t)((double)(n - 1u) * 0.99)];
    out->stddev_us = (n > 1u) ? sqrt(sq_sum / (double)n) : 0.0;
}

static inline void BenchSummaryPrint(const char *label, const bench_summary_t *s)
{
    printf("  %-40s n=%-6zu min=%9.2f  mean=%9.2f  median=%9.2f  p95=%9.2f  p99=%9.2f  max=%9.2f  stddev=%8.2f (us)\n",
           label, s->n, s->min_us, s->mean_us, s->median_us, s->p95_us, s->p99_us, s->max_us, s->stddev_us);
}

#ifdef __cplusplus
}
#endif

#endif /* BENCH_STATS_H */
