#ifndef AOS_PORT_H
#define AOS_PORT_H

/* Afligh8_osal port: POSIX on Linux (also the host build for SITL/CI). */

#define AOS_PORT_NAME               "posix"

/* Threads on a hosted OS are cheap; 64 KiB matches what the tests use. */
#define AOS_PORT_TASK_STACK_DEFAULT (64u * 1024u)
#define AOS_PORT_TASK_STACK_MIN     (16u * 1024u)   /* glibc PTHREAD_STACK_MIN */

/*
 * Kept on CLOCK_REALTIME for now so this port's behaviour is unchanged.
 * glibc >= 2.30 offers sem_clockwait(); flip to 1 once the switch is
 * agreed for both ports.
 */
#define AOS_PORT_SEM_USE_CLOCKWAIT  0

#endif /* AOS_PORT_H */
