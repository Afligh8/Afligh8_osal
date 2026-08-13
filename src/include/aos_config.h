#ifndef AOS_CONFIG_H
#define AOS_CONFIG_H

/*
 * Number of logical AOS task priority levels.
 *
 * AOS priority convention:
 *   0  = highest priority
 *   31 = lowest priority
 *
 * 32 levels map cleanly into the Linux RT priority range.
 */
#define AOS_CONFIG_TASK_PRIORITY_LEVELS 32u

/*
 * POSIX real-time scheduling behavior.
 *
 * 0:
 *   Try SCHED_FIFO/SCHED_RR.
 *   Fall back to SCHED_OTHER if RT scheduling is unavailable.
 *
 * 1:
 *   RT scheduling is mandatory.
 *   Initialization/task creation fails if RT scheduling cannot be used.
 */
#ifndef AOS_CONFIG_POSIX_RT_REQUIRED
#define AOS_CONFIG_POSIX_RT_REQUIRED 0
#endif

/*
 * Print a warning when RT scheduling cannot be enabled and
 * the POSIX backend falls back to SCHED_OTHER.
 */
#ifndef AOS_CONFIG_POSIX_RT_WARN_FALLBACK
#define AOS_CONFIG_POSIX_RT_WARN_FALLBACK 1
#endif

#endif /* AOS_CONFIG_H */