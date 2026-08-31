#include "aos_osal.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;

#define CHECK(id, desc, cond)                                       \
    do {                                                            \
        int _ok = (cond);                                           \
        printf("  [%s] %-15s %s\n", _ok ? "PASS" : "FAIL", id, desc); \
        if (!_ok) fails++;                                          \
    } while (0)
 
int main(void){

    aos_id_t id = AOS_IdPack(AOS_TYPE_MUTEX, 1, 0);
    uint32_t caps = 0u;
    int32_t caps_status;

    printf("M0 core: ticket round-trip + init\n\n");

    CHECK("AOS-CORE-000",  "real ticket is non-zero", id != AOS_ID_NONE);
    CHECK("AOS-CORE-001a", "type round-trips", AOS_IdType(id) == AOS_TYPE_MUTEX);
    CHECK("AOS-CORE-001b", "serial round-trips", AOS_IdSerial(id) == 1);
    CHECK("AOS-CORE-001c", "index round-trips", AOS_IdIndex(id) == 0);
    CHECK("AOS-CORE-002",  "NONE has type NONE", AOS_IdType(AOS_ID_NONE) == AOS_TYPE_NONE);
    CHECK("AOS-CORE-003",  "init succeeds", AOS_Init() == AOS_SUCCESS);
    CHECK("AOS-CORE-004",  "init is idempotent", AOS_Init() == AOS_SUCCESS);

    caps_status = AOS_GetCapabilities(&caps);

    CHECK("AOS-CORE-005",  "GetCapabilities rejects NULL", AOS_GetCapabilities(NULL) == AOS_ERR_INVALID_POINTER);
    CHECK("AOS-CORE-006",  "GetCapabilities succeeds", caps_status == AOS_SUCCESS);
    CHECK("AOS-CORE-007",  "priority inheritance capability", (caps & AOS_CAP_PRIORITY_INHERIT) != 0u);
    CHECK("AOS-CORE-008",  "timed wait capability", (caps & AOS_CAP_TIMED_WAIT) != 0u);
    CHECK("AOS-CORE-009",  "monotonic clock capability", (caps & AOS_CAP_MONOTONIC_CLOCK) != 0u);

    /*
     * Not a CHECK: whether this bit is set is a fact about the current
     * process's privileges (e.g. rtprio ulimit / CAP_SYS_NICE), not a
     * defect either way. See AOS_PosixTaskIsRtEnabled() and
     * tests/test_rt_required.c, which asserts the correct status code
     * for whichever outcome this environment produces.
     */
    printf("  RT scheduling capability: %s (depends on process privilege)\n",
           (caps & AOS_CAP_RT_SCHEDULING) ? "available" : "unavailable");

    CHECK("AOS-CORE-010",  "permission denied decodes",
          strcmp(AOS_StrError(AOS_ERR_PERMISSION_DENIED), "AOS_ERR_PERMISSION_DENIED") == 0);
    CHECK("AOS-CORE-011",  "unsupported decodes",
          strcmp(AOS_StrError(AOS_ERR_UNSUPPORTED), "AOS_ERR_UNSUPPORTED") == 0);
    CHECK("AOS-CORE-012",  "unknown code falls back",
          strcmp(AOS_StrError(-12345), "AOS_ERR_UNKNOWN") == 0);

    printf("  decoder: %d -> %s\n", (int)AOS_ERR_INVALID_POINTER, AOS_StrError(AOS_ERR_INVALID_POINTER));
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    
    return fails ? 1 : 0;
}