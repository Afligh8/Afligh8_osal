#include "aos_osal.h"

#include <stdio.h>

/*
 * This binary is built against a SEPARATE copy of the OSAL library
 * compiled with AOS_CONFIG_POSIX_RT_REQUIRED=1 (see CMakeLists.txt's
 * aos_osal_rt_required target) -- the normal aos_osal library used by
 * every other test keeps the default (0), so this is the only place
 * that exercises AOS_PosixConfigureRtPolicy()'s "RT is mandatory"
 * branch at all.
 *
 * That branch's outcome is genuinely environment-dependent (it depends
 * on whether this process has permission to use SCHED_FIFO/RR), so
 * unlike most tests this one doesn't assert a single expected result --
 * it asserts that whichever of the two legitimate outcomes occurs, AOS
 * reports it with the correct, specific status code rather than a
 * generic one.
 */
int main(void)
{
    int32_t status = AOS_Init();

    if (status == AOS_SUCCESS)
    {
        uint32_t caps = 0u;

        if (AOS_GetCapabilities(&caps) != AOS_SUCCESS)
        {
            fprintf(stderr, "AOS_GetCapabilities failed after AOS_Init succeeded\n");
            return 1;
        }

        if (!(caps & AOS_CAP_RT_SCHEDULING))
        {
            fprintf(stderr,
                    "AOS_Init succeeded under AOS_CONFIG_POSIX_RT_REQUIRED=1 "
                    "but AOS_CAP_RT_SCHEDULING is clear -- contradiction\n");
            return 2;
        }

        printf("RT scheduling privilege available: AOS_Init succeeded and "
               "AOS_CAP_RT_SCHEDULING is set. PASS\n");

        return 0;
    }

    if (status == AOS_ERR_PERMISSION_DENIED)
    {
        printf("RT scheduling privilege unavailable in this environment: "
               "AOS_Init correctly reported AOS_ERR_PERMISSION_DENIED "
               "instead of a generic failure. PASS\n");

        return 0;
    }

    fprintf(stderr,
            "AOS_Init under AOS_CONFIG_POSIX_RT_REQUIRED=1 returned an "
            "unexpected code: %s\n",
            AOS_StrError(status));

    return 3;
}
