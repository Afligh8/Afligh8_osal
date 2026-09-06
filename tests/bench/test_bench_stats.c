#include "bench_stats.h"

#include <stdio.h>

/*
 * Self-test for the stats utility itself, using known values --
 * before trusting this to characterize real OSAL timing, it needs to
 * correctly characterize numbers we already know the answer to.
 */

static bench_series_t g_series;

static int CheckClose(const char *what, double got, double expected, double tol)
{
    double diff = got - expected;

    if (diff < 0.0) {
        diff = -diff;
    }

    if (diff > tol) {
        fprintf(stderr, "FAIL %-10s got=%.4f expected=%.4f (tol=%.4f)\n", what, got, expected, tol);
        return 0;
    }

    printf("PASS %-10s got=%.4f expected=%.4f\n", what, got, expected);
    return 1;
}

int main(void)
{
    bench_summary_t summary;
    int i;
    int ok = 1;

    BenchSeriesReset(&g_series);

    /* Samples 1..100 (us) -- every summary statistic below has a known
     * closed-form answer for this exact sequence. */
    for (i = 1; i <= 100; ++i) {
        if (!BenchSeriesAdd(&g_series, (aos_time_t)i)) {
            fprintf(stderr, "FAIL BenchSeriesAdd rejected a sample within capacity\n");
            return 1;
        }
    }

    if (g_series.count != 100u || g_series.dropped != 0u) {
        fprintf(stderr, "FAIL count/dropped bookkeeping wrong: count=%zu dropped=%zu\n",
                g_series.count, g_series.dropped);
        return 1;
    }

    BenchSeriesSummarize(&g_series, &summary);

    ok &= CheckClose("n",      (double)summary.n, 100.0, 0.0);
    ok &= CheckClose("min",    summary.min_us, 1.0, 0.0);
    ok &= CheckClose("max",    summary.max_us, 100.0, 0.0);
    ok &= CheckClose("mean",   summary.mean_us, 50.5, 0.001);
    ok &= CheckClose("median", summary.median_us, 51.0, 0.0);
    ok &= CheckClose("p95",    summary.p95_us, 95.0, 0.0);
    ok &= CheckClose("p99",    summary.p99_us, 99.0, 0.0);
    /* Population stddev of 1..n is sqrt((n^2-1)/12); for n=100 that's
     * sqrt(9999/12) = 28.8661... */
    ok &= CheckClose("stddev", summary.stddev_us, 28.8661, 0.01);

    /* Overflow behavior: capacity is BENCH_MAX_SAMPLES -- fill the rest
     * and confirm exactly one more sample gets rejected and counted as
     * dropped, not silently lost or accepted past capacity. */
    {
        bench_series_t full;
        size_t j;

        BenchSeriesReset(&full);

        for (j = 0u; j < BENCH_MAX_SAMPLES; ++j) {
            if (!BenchSeriesAdd(&full, (aos_time_t)j)) {
                fprintf(stderr, "FAIL rejected a sample before reaching capacity (at %zu)\n", j);
                return 1;
            }
        }

        if (BenchSeriesAdd(&full, (aos_time_t)999999)) {
            fprintf(stderr, "FAIL accepted a sample past BENCH_MAX_SAMPLES\n");
            return 1;
        }

        if (full.count != BENCH_MAX_SAMPLES || full.dropped != 1u) {
            fprintf(stderr, "FAIL overflow bookkeeping wrong: count=%zu dropped=%zu\n",
                    full.count, full.dropped);
            return 1;
        }

        printf("PASS overflow    count=%zu dropped=%zu\n", full.count, full.dropped);
    }

    if (!ok) {
        return 1;
    }

    puts("ALL BENCH_STATS SELF-TESTS PASSED");

    return 0;
}
