/* test_time.c — M1: prove AOS_TimeGet. */
#define _POSIX_C_SOURCE 200809L

#include "aos_osal.h"
#include <stdio.h>
#include <time.h>

static int fails = 0;
#define CHECK(id, desc, cond)                                       \
    do {                                                            \
        int _ok = (cond);                                           \
        printf("  [%s] %-14s %s\n", _ok ? "PASS" : "FAIL", id, desc); \
        if (!_ok) fails++;                                          \
    } while (0)

int main(void)
{
    aos_time_t t0 = 0, t1 = 0;
    struct timespec nap = { 0, 20 * 1000 * 1000 };   /* 20 ms */

    printf("M1 time: AOS_TimeGet\n\n");
    AOS_Init();

    CHECK("AOS-TIME-001",  "NULL -> INVALID_POINTER", AOS_TimeGet(NULL) == AOS_ERR_INVALID_POINTER);
    CHECK("AOS-TIME-002",  "valid -> SUCCESS",        AOS_TimeGet(&t0)  == AOS_SUCCESS);

    nanosleep(&nap, NULL);
    AOS_TimeGet(&t1);

    CHECK("AOS-TIME-003",  "monotonic: t1 > t0",      t1 > t0);
    CHECK("AOS-TIME-003b", "elapsed >= 15 ms",        (t1 - t0) >= 15000);
    printf("  (measured elapsed: %lld us)\n", (long long)(t1 - t0));

    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}