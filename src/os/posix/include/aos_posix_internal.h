#ifndef AOS_POSIX_INTERNAL_H
#define AOS_POSIX_INTERNAL_H

#include "aos_osal.h"

#include <stdbool.h>

int32_t AOS_PosixTaskInit(void);
int32_t AOS_PosixMutexInit(void);
int32_t AOS_PosixBinSemInit(void);

/*
 * Whether AOS_PosixConfigureRtPolicy() actually secured SCHED_FIFO/RR
 * during AOS_PosixTaskInit(). Backs AOS_CAP_RT_SCHEDULING -- this is the
 * one capability bit that's a genuine runtime probe result rather than
 * a compile-time POSIX feature check, since it depends on the calling
 * process's privileges, not just what this libc/kernel builds support.
 */
bool AOS_PosixTaskIsRtEnabled(void);

#endif /* AOS_POSIX_INTERNAL_H */