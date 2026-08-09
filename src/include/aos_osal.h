/*
 * aos_osal - AOS OSAL API
 * author yahayahusseini321@gmail.com
 * Naming convention (FROZEN — applies to every aos_osal header):
 *   - Types:            aos_<name>_t        lower_snake        (aos_id_t, aos_time_t)
 *   - Functions:        AOS_<Name>          AOS_ + PascalCase  (AOS_Init, AOS_TimeGet)
 *                       (inline helpers follow the same rule,  e.g. AOS_IdPack)
 *   - Constants/macros: AOS_<UPPER_SNAKE>                      (AOS_MAX_TASKS, AOS_ID_NONE)
 *   - Result codes:     every failure is AOS_ERR_*; AOS_SUCCESS is the only non-ERR code.
 *
 * The status enum exists only to NAME the integer result codes. Every API
 * function returns int32_t (never the enum), so the ABI is fixed regardless
 * of the enum's implementation-defined underlying type. Compare returns
 * against these constants.
*/

#ifndef AOS_OSAL_H
#define AOS_OSAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum aos_status {
    AOS_SUCCESS                 =   0,
    AOS_ERR_GENERIC             =  -1,   /* unspecified failure          (M1) */
    AOS_ERR_INVALID_POINTER     =  -2,   /* a required pointer was NULL  (M1) */
    AOS_ERR_INVALID_ID          =  -5,
    AOS_ERR_INVALID_SIZE        =  -6,
    AOS_ERR_NAME_TOO_LONG       =  -7,
    AOS_ERR_NAME_TAKEN          =  -8,
    AOS_ERR_NO_FREE_IDS         =  -9,
    AOS_ERR_INVALID_PRIORITY    = -10,
    AOS_ERR_INVALID_PARAM       = -11,
    AOS_ERR_CREATION_FAILED     = -12,
    AOS_ERR_TIMEOUT             = -16    /* wait primitive timed out (any object) */
};

/*
 * AOS_StrError — decode a result code to a stable, human-readable name.
 *   code    : any int32_t; unknown codes yield a generic "unknown" string.
 *   returns : pointer to static NUL-terminated storage (never NULL).
 *   ISR-safe: yes.  blocking: no.  thread-safe: yes (static storage).
 */
const char *AOS_StrError(int32_t code);

/*
 * Static configuration — object tables live in .bss (no dynamic allocation).
 *
 * AOS_MAX_NAME is the buffer size INCLUDING the NUL terminator, so the
 * longest usable name is (AOS_MAX_NAME - 1) = 19 characters. A name whose
 * strlen exceeds that yields AOS_ERR_NAME_TOO_LONG.
*/
#define AOS_MAX_NAME     20
#define AOS_MAX_TASKS     8
#define AOS_MAX_MUTEXES   8
#define AOS_MAX_SEMS      8
 
/* Opaque object id (ticket).
 * One uint32_t packs { type[31:24], serial[23:16], index[15:0] }.
 * A live id is always non-zero (type >= 1), so 0 is a safe "none".
 *
 * The serial field makes stale ids detectable: an id captured before its
 * slot is freed will not match the slot's next occupant. Serial is 8-bit,
 * so detection is best-effort — it can alias after 256 reuses of the same
 * slot. Ample margin for an FC that rarely churns objects; NOT a hard
 * guarantee.
 */
typedef uint32_t aos_id_t;
#define AOS_ID_NONE  ((aos_id_t)0)

enum {
    AOS_TYPE_NONE   = 0,
    AOS_TYPE_TASK   = 1,
    AOS_TYPE_MUTEX  = 2,
    AOS_TYPE_BINSEM = 3
};

/* Id pack/unpack helpers — inline so every module and test shares one
 * definition. All pure: ISR-safe, non-blocking, thread-safe.
 */
static inline aos_id_t AOS_IdPack(uint8_t type, uint8_t serial, uint16_t index)
{
    return ((aos_id_t)type << 24) | ((aos_id_t)serial << 16) | (aos_id_t)index;
}
static inline uint8_t  AOS_IdType  (aos_id_t id) { return (uint8_t) ((id >> 24) & 0xFFu); }
static inline uint8_t  AOS_IdSerial(aos_id_t id) { return (uint8_t) ((id >> 16) & 0xFFu); }
static inline uint16_t AOS_IdIndex (aos_id_t id) { return (uint16_t)( id        & 0xFFFFu); }
 
/* lifecycle 
 * AOS_Init — initialise the OSAL. Must be called once before any other AOS_*
 * call. Idempotent: repeated calls are no-ops that return AOS_SUCCESS, so
 * independent subsystems may each call it defensively.
 *   returns : AOS_SUCCESS, or AOS_ERR_* on failure.
 *   ISR-safe: no (call once at startup).  blocking: no.
 */
int32_t AOS_Init(void);
 
/* time */
typedef int64_t aos_time_t;               /* microseconds, monotonic */

/*
 * AOS_TimeGet — read the monotonic clock. The epoch is arbitrary (not
 * wall-clock, no fixed zero); only the difference between two readings is
 * meaningful. Guaranteed non-decreasing.
 *   now_us  : out; receives the current time. NULL -> AOS_ERR_INVALID_POINTER.
 *   returns : AOS_SUCCESS, or AOS_ERR_* on failure.
 *   ISR-safe: yes.  blocking: no.
 */

int32_t AOS_TimeGet(aos_time_t *now_us);  /* writes current time; NULL -> INVALID_POINTER */

#ifdef __cplusplus
}
#endif
#endif // AOS_OSAL_H