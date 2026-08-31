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
    
    printf("M0 core: ticket round-trip + init\n\n");

    CHECK("AOS-CORE-000",  "real ticket is non-zero", id != AOS_ID_NONE);
    CHECK("AOS-CORE-001a", "type round-trips", AOS_IdType(id) == AOS_TYPE_MUTEX);
    CHECK("AOS-CORE-001b", "serial round-trips", AOS_IdSerial(id) == 1);
    CHECK("AOS-CORE-001c", "index round-trips", AOS_IdIndex(id) == 0);
    CHECK("AOS-CORE-002",  "NONE has type NONE", AOS_IdType(AOS_ID_NONE) == AOS_TYPE_NONE);
    CHECK("AOS-CORE-003",  "init succeeds", AOS_Init() == AOS_SUCCESS);
    CHECK("AOS-CORE-004",  "init is idempotent", AOS_Init() == AOS_SUCCESS);
    CHECK("AOS-CORE-005",  "permission denied decodes",
          strcmp(AOS_StrError(AOS_ERR_PERMISSION_DENIED), "AOS_ERR_PERMISSION_DENIED") == 0);
    CHECK("AOS-CORE-006",  "unsupported decodes",
          strcmp(AOS_StrError(AOS_ERR_UNSUPPORTED), "AOS_ERR_UNSUPPORTED") == 0);
    CHECK("AOS-CORE-007",  "unknown code falls back",
          strcmp(AOS_StrError(-12345), "AOS_ERR_UNKNOWN") == 0);

    printf("  decoder: %d -> %s\n", (int)AOS_ERR_INVALID_POINTER, AOS_StrError(AOS_ERR_INVALID_POINTER));
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    
    return fails ? 1 : 0;
}