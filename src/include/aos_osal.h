/*
    aos_osal - AOS OSAL API
*/

#ifndef AOS_OSAL_H
#define AOS_OSAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*Status Vocabulary*/
#define AOS_SUCCESS             ( 0)
#define AOS_ERROR               (-1)
#define AOS_INVALID_POINTER     (-2)
#define AOS_ERR_INVALID_SIZE    (-3)
#define AOS_SEM_TIMEOUT         (-7)
#define AOS_ERR_NAME_TOO_LONG   (-13)
#define AOS_ERR_NO_FREE_IDS     (-14)
#define AOS_ERR_NAME_TAKEN      (-15)
#define AOS_ERR_INVALID_ID      (-16)
#define AOS_ERR_INVALID_PRIORITY (-19)
 
/*static configuration (object tables live in .bss)*/
#define AOS_MAX_NAME     20
#define AOS_MAX_TASKS     8
#define AOS_MAX_MUTEXES   8
#define AOS_MAX_SEMS      8
 
/* The opaque ticket One uint32_t packs {type[31:24], serial[23:16], index[15:0]}.
 * A real ticket is always non-zero (type >= 1), so 0 is a safe "none".
 */
typedef uint32_t aos_id_t;
#define AOS_ID_NONE  ((aos_id_t)0)

enum {
    AOS_TYPE_NONE   = 0,
    AOS_TYPE_TASK   = 1,
    AOS_TYPE_MUTEX  = 2,
    AOS_TYPE_BINSEM = 3
};

/* pack / unpack helpers (inline so tests and every module share one definition) */
static inline aos_id_t aos_id_pack(uint8_t type, uint8_t serial, uint16_t index)
{
    return ((aos_id_t)type << 24) | ((aos_id_t)serial << 16) | (aos_id_t)index;
}
static inline uint8_t  aos_id_type  (aos_id_t id) { return (uint8_t) ((id >> 24) & 0xFFu); }
static inline uint8_t  aos_id_serial(aos_id_t id) { return (uint8_t) ((id >> 16) & 0xFFu); }
static inline uint16_t aos_id_index (aos_id_t id) { return (uint16_t)( id        & 0xFFFFu); }
 
/* lifecycle */
int32_t AOS_Init(void);
 
/* time */
typedef int64_t aos_time_t;               /* microseconds, monotonic */
int32_t AOS_TimeGet(aos_time_t *now_us);  /* writes current time; NULL -> INVALID_POINTER */
#ifdef __cplusplus
}
#endif
#endif // AOS_OSAL_H