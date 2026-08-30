#ifndef AOS_POSIX_INTERNAL_H
#define AOS_POSIX_INTERNAL_H

#include "aos_osal.h"

int32_t AOS_PosixTaskInit(void);
int32_t AOS_PosixMutexInit(void);
int32_t AOS_PosixBinSemInit(void);

#endif /* AOS_POSIX_INTERNAL_H */