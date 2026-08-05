#include "aos_osal.h"
#include <stdio.h>

static int fails = 0;

#define CHECK(id, desc, cond)                                       \
    do {                                                            \
        int _ok = (cond);                                           \
        printf("  [%s] %-15s %s\n", _ok ? "PASS" : "FAIL", id, desc); \
        if (!_ok) fails++;                                          \
    } while (0)
 
int main(){
    
    printf("M0 core: ticket round-trip + init\n\n");

    aos_id_t id = aos_id_pack(AOS_TYPE_MUTEX, 1, 0);
 
    CHECK("AOS-CORE-000",  "real ticket is non-zero",  id != AOS_ID_NONE);
    CHECK("AOS-CORE-001a", "type round-trips",         aos_id_type(id)   == AOS_TYPE_MUTEX);
    CHECK("AOS-CORE-001b", "serial round-trips",       aos_id_serial(id) == 1);
    CHECK("AOS-CORE-001c", "index round-trips",        aos_id_index(id)  == 0);
    CHECK("AOS-CORE-002",  "NONE has type NONE",       aos_id_type(AOS_ID_NONE) == AOS_TYPE_NONE);
    CHECK("AOS-CORE-003",  "init succeeds",            AOS_Init() == AOS_SUCCESS);
 
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}