#include <stdio.h>
#include <aos_task.h>

#include "bench_stats.h"

#define BENCH_WARMUP_CYCLES 2000
#define BENCH_MEASURE_N     (BENCH_MAX_SAMPLES - BENCH_WARMUP_CYCLES)

typedef struct {
    aos_time_t start_time;
} task_bench_arg_t;

static bench_series_t create_series;
static bench_series_t sched_series;
static bench_series_t join_series;
static bench_series_t delete_series;

static bench_summary_t summary;

// task entry point
void benchmark_task_entry(void *arg){
    task_bench_arg_t *targ = (task_bench_arg_t *)arg;

    if (targ != NULL){
        //Critical: capture timestamp immediately upon task startup 
        AOS_TimeGet(&targ->start_time);
    }
    //Task body remains minimal
}

// Main Benchmark Function
int run_osal_benchmark_suite(void){

    aos_task_t task_id = AOS_TASK_NONE;
    task_bench_arg_t targ;

    //reset all mearsurement series
    BenchSeriesReset(&create_series);
    BenchSeriesReset(&sched_series);
    BenchSeriesReset(&join_series);
    BenchSeriesReset(&delete_series);

    // PHASE 1: Warm-Up Phase (2,000 cycles)
    // Justification: Flushes L1/L2 instruction caches, pre-allocates TCB heap 
    // structures, and stabilizes scheduler priority queues.
    for (uint32_t i = 0u; i < BENCH_WARMUP_CYCLES; i++){

        targ.start_time = 0;

        if (AOS_TaskCreate(&task_id, "Warmpup task", benchmark_task_entry,
                            NULL, NULL, 64u * 1024u, AOS_TASK_PRIORITY_DEFAULT,
                            AOS_TASK_FLAG_NONE) != AOS_SUCCESS){
            return 2;
        }

        if (AOS_TaskJoin(task_id) != AOS_SUCCESS){
            return 3;
        }

        if (AOS_TaskDelete(task_id) != AOS_SUCCESS){
            return 4;
        }

    }

    printf("Finished Bench warmup cycles\n");

    int32_t negative_case = 0;

    // PHASE 2: Measured Phase (18,000 cycles recorded)
    for(uint32_t j = 0u; j < BENCH_MEASURE_N; j++){
        targ.start_time = 0u;
        aos_time_t t0, t1, t2, t3, t4, t5;

        // 1. Measure AOS_TaskCreate
        AOS_TimeGet(&t0);
        int32_t status = AOS_TaskCreate(&task_id, "bench_task", benchmark_task_entry, 
                                             &targ, NULL, 64u * 1024u, 
                                             AOS_TASK_PRIORITY_DEFAULT, AOS_TASK_FLAG_NONE);
        AOS_TimeGet(&t1);

        if (status != AOS_SUCCESS) {
            fprintf(stderr, "Failed to Create Task at cycle %u: err=%d\n", j, status);
            return 5;
        }
        
        // 2. Measure AOS_TaskJoin
        // each task also have to be joined and deleted, as we can only create AOS_TASK_MAX tasks
        // else it will return -8 AOS_ERR_NAME_TAKEN
        AOS_TimeGet(&t2);
        if (AOS_TaskJoin(task_id) != AOS_SUCCESS) {
            return 6;
        }
        AOS_TimeGet(&t3);

        // 3. Measure AOS_TaskDelete
        AOS_TimeGet(&t4);
        if (AOS_TaskDelete(task_id) != AOS_SUCCESS) {
            return 7;
        }
        AOS_TimeGet(&t5);

        // Calculate & Store Metrics into bench_series_t 
        //Create cost
        BenchSeriesAdd(&create_series, (t1 - t0));

        // Scheduling Latency (Time between TaskCreate returning & task entry executing)
        if (targ.start_time > t1){
            BenchSeriesAdd(&sched_series, targ.start_time - t1);
        } else {
            negative_case ++;

        }
        
        // Join Cost
        BenchSeriesAdd(&join_series, (t3 - t2));

        // Delete Cost
        BenchSeriesAdd(&delete_series, (t5 - t4));
        }

        // PHASE 3: Summarize & Print Statistics
        printf("\n OSAL TASK LIFECYCLE BENCHMARK RESULTS \n");

        BenchSeriesSummarize(&create_series, &summary);
        BenchSummaryPrint("create_series", &summary);

        BenchSeriesSummarize(&sched_series, &summary);
        BenchSummaryPrint("sched_series", &summary);

        BenchSeriesSummarize(&join_series, &summary);
        BenchSummaryPrint("join_series", &summary);

        BenchSeriesSummarize(&delete_series, &summary);
        BenchSummaryPrint("delete_series", &summary);

        printf ("\n Negative case : %d\n", negative_case);
    return 0;
}

int main(){

    int ret = AOS_Init();

    if (ret != AOS_SUCCESS)
    {
        fprintf(stderr, "AOS_Init failed: %s\n",
        AOS_StrError(ret));
        return 1;
    }

    int status = run_osal_benchmark_suite();
    
    if (status != 0)
    {
        printf("\n Benchmark failed during execution : %d\n ", status);
    }

    return 0;
}