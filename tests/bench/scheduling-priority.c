#include <stdio.h>
#include <aos_task.h>
#include <aos_sem.h>

#include "bench_stats.h"

#include <sched.h>
#include <pthread.h>

#define BENCH_WARMUP_CYCLES 2000u
#define BENCH_MEASURE_N     (BENCH_MAX_SAMPLES - BENCH_WARMUP_CYCLES)

typedef struct {
    aos_time_t start_time;
} task_bench_arg_t;

static bench_series_t create_series;
static bench_series_t sched_series;
static bench_series_t join_series;
static bench_series_t delete_series;

static bench_summary_t summary;

volatile double accumulator_var = 0.0;

volatile aos_time_t total_time;

aos_sem_t ready_sem  = AOS_SEM_NONE;

//Simulating a fixed cpu-bound workload

// task entry point
void benchmark_task_entry(void *arg){

    // int policy;
    // struct sched_param param;
    // pthread_getschedparam(pthread_self(), &policy, &param);
    // printf("worker thread policy=%d priority=%d\n", policy, param.sched_priority);
            
    AOS_SemWait(ready_sem);

    aos_time_t exec_start = AOS_TimeGet(&exec_start);
    
    task_bench_arg_t *targ = (task_bench_arg_t *)arg;

    if (targ != NULL){
        AOS_TimeGet(&targ->start_time);
    }

    //do something
    for (size_t i = 0; i < 500; i++)
    {
        accumulator_var += (50 * 3.14159) / 2.567;
    }
    aos_time_t exec_end = AOS_TimeGet(&exec_end);
    
    total_time = exec_end - exec_start;
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

    // Warm-Up Phase (2,000 cycles)
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

    // PHASE A: Measured Phase (18,000 cycles recorded)
    for(uint32_t j = 0u; j < BENCH_MEASURE_N; j++){

        int32_t status = 0;

        //
        status = AOS_SemCreate(&ready_sem, "ready-sem", AOS_SEM_EMPTY, AOS_SEM_FLAG_NONE);

        targ.start_time = 0u;
        aos_time_t t0, t1, t2, t3, t4, t5;

        // 1. Measure AOS_TaskCreate
        AOS_TimeGet(&t0);
        status = AOS_TaskCreate(&task_id, "bench_task", benchmark_task_entry, 
                                             &targ, NULL, 64u * 1024u, 
                                             AOS_TASK_PRIORITY_DEFAULT, AOS_TASK_FLAG_NONE);
        AOS_TimeGet(&t1);

        if (status != AOS_SUCCESS) {
            fprintf(stderr, "Failed to Create Task at cycle %u: err=%d\n", j, status);
            return 5;
        }

        //
        AOS_SemPost(ready_sem);
        
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

        //
        AOS_SemDelete(ready_sem);


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

    printf("\n PHASE 1 \n");

    BenchSeriesSummarize(&create_series, &summary);
    BenchSummaryPrint("create_series", &summary);

    BenchSeriesSummarize(&sched_series, &summary);
    BenchSummaryPrint("sched_series", &summary);

    BenchSeriesSummarize(&join_series, &summary);
    BenchSummaryPrint("join_series", &summary);

    BenchSeriesSummarize(&delete_series, &summary);
    BenchSummaryPrint("delete_series", &summary);

    printf ("\n Negative case : %d\n", negative_case);


    // PHASE B: Measured Phase (18,000 cycles recorded)
    aos_task_info_t info;
    // aos_sem_state_t sem_state;
    
    int32_t negative_case2 = 0;

    //reset all mearsurement series
    BenchSeriesReset(&create_series);
    BenchSeriesReset(&sched_series);
    BenchSeriesReset(&join_series);
    BenchSeriesReset(&delete_series);
    
    for(uint32_t k = 0u; k < BENCH_MEASURE_N; k++){
        int32_t status = 0;

        //
        status = AOS_SemCreate(&ready_sem, "ready-sem", AOS_SEM_EMPTY, AOS_SEM_FLAG_NONE);

        if (status != AOS_SUCCESS){
            printf("Failed to create Semaphore");
            return 8;
        }
        
        targ.start_time = 0u;
        aos_time_t t0, t1, t2, t3, t4, t5;
        
        AOS_TimeGet(&t0);
        status = AOS_TaskCreate(&task_id, "bench_task2", benchmark_task_entry, 
                                             &targ, NULL, 64u * 1024u, 
                                             AOS_TASK_PRIORITY_DEFAULT, AOS_TASK_FLAG_NONE);
        AOS_TimeGet(&t1);
        if (status != AOS_SUCCESS) {
            fprintf(stderr, "Failed to Create Task at cycle %u: err=%d\n", k, status);
            return 9;
        }

        status = AOS_TaskSetPriority(task_id, AOS_TASK_PRIORITY_HIGHEST);

        if (status != AOS_SUCCESS){
            fprintf(stderr, "Failed to Set priority to highest, err:%d\n", status);
            return 10;
        }

        //
        AOS_SemPost(ready_sem);

        AOS_TaskGetInfo(task_id, &info);
        
        AOS_TimeGet(&t2);
        if (AOS_TaskJoin(task_id) != AOS_SUCCESS) {
            return 11;
        }
        AOS_TimeGet(&t3);

        AOS_TimeGet(&t4);
        if (AOS_TaskDelete(task_id) != AOS_SUCCESS) {
            return 12;
        }
        AOS_TimeGet(&t5);

        //
        AOS_SemDelete(ready_sem);

        BenchSeriesAdd(&create_series, (t1 - t0));

        // Scheduling Latency (Time between TaskCreate returning & task entry executing)
        if (targ.start_time > t1){
            BenchSeriesAdd(&sched_series, targ.start_time - t1);
        } else {
            negative_case2 ++;
        }
        
        BenchSeriesAdd(&join_series, (t3 - t2));

        BenchSeriesAdd(&delete_series, (t5 - t4));
    }

    printf("\n PHASE 2 \n");

    BenchSeriesSummarize(&create_series, &summary);
    BenchSummaryPrint("create_series", &summary);

    BenchSeriesSummarize(&sched_series, &summary);
    BenchSummaryPrint("sched_series", &summary);

    BenchSeriesSummarize(&join_series, &summary);
    BenchSummaryPrint("join_series", &summary);

    BenchSeriesSummarize(&delete_series, &summary);
    BenchSummaryPrint("delete_series", &summary);

    printf("TASK INFO: name = %s , priority=%d\n", info.name, info.priority);

    printf ("\n Negative case2 : %d\n", negative_case2);

    printf("Accumulated var : %f\n", accumulator_var);

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