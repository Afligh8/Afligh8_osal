# Afligh8_osal -- NuttX apps glue (CMake build)
#
# Makes the OSAL a NuttX library and, optionally, its tests NSH builtin
# commands. Reachable from a NuttX apps tree in either of two ways:
#
#   1. Standalone (this repo's own simulator tests): symlink
#      <nuttx-apps>/external -> <this repo>/nuttx, so this file IS
#      apps/external/CMakeLists.txt. tests/nuttx/run_sim.sh does that.
#   2. From an application (the Afligh8 flight controller): its own
#      apps/CMakeLists.txt does add_subdirectory(<path to this dir> aos_osal).
#
# Kconfig (nuttx/Kconfig next to this file):
#   CONFIG_AOS_OSAL              build the library
#   CONFIG_AOS_OSAL_TESTS        also register the test suite as NSH commands
#   CONFIG_AOS_NUTTX_PRIORITY_*  the NuttX priority window AOS priorities map onto
#
# The backend is src/os/nuttx (a thin port over the shared pthread-family
# core); see src/os/nuttx/include/aos_port.h for the kernel options it
# requires and src/os/nuttx/aos_osal.defconfig for a fragment that sets them.

if(CONFIG_AOS_OSAL)

  # This directory is usually reached through a symlink (apps/external ->
  # <repo>/nuttx); CMAKE_CURRENT_LIST_DIR is the symlink's path, so resolve
  # it before going up to the repository root.
  get_filename_component(AOS_GLUE_DIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
  get_filename_component(AOS_ROOT "${AOS_GLUE_DIR}/.." REALPATH)
  include("${AOS_ROOT}/cmake/AosSources.cmake")

  aos_backend_sources(nuttx AOS_NUTTX_SOURCES)
  aos_backend_private_includes(nuttx AOS_NUTTX_PRIVATE_INCLUDES)
  list(TRANSFORM AOS_NUTTX_SOURCES PREPEND "${AOS_ROOT}/")
  list(TRANSFORM AOS_NUTTX_PRIVATE_INCLUDES PREPEND "${AOS_ROOT}/")

  nuttx_add_library(aos_osal STATIC)
  target_sources(aos_osal PRIVATE ${AOS_NUTTX_SOURCES})
  target_include_directories(aos_osal
    PUBLIC  "${AOS_ROOT}/src/include"
    PRIVATE ${AOS_NUTTX_PRIVATE_INCLUDES})
  target_compile_options(aos_osal PRIVATE -Wall -Wextra)
  # Only forward the Kconfig window when the build exposes it; otherwise the
  # port header's defaults apply. An empty -D would define the macro to
  # nothing and break every expression that uses it.
  if(CONFIG_AOS_NUTTX_PRIORITY_HIGHEST)
    target_compile_definitions(aos_osal PRIVATE
      AOS_CONFIG_NUTTX_PRIORITY_HIGHEST=${CONFIG_AOS_NUTTX_PRIORITY_HIGHEST})
  endif()
  if(CONFIG_AOS_NUTTX_PRIORITY_LOWEST)
    target_compile_definitions(aos_osal PRIVATE
      AOS_CONFIG_NUTTX_PRIORITY_LOWEST=${CONFIG_AOS_NUTTX_PRIORITY_LOWEST})
  endif()

  if(CONFIG_AOS_OSAL_TESTS)
    # One NSH command per test binary, same names as the host ctest targets.
    # Each test's main() returns 0 on pass, so `test_core; echo $?` in NSH
    # (or the driver in tests/nuttx/run_sim.sh) can tell.
    set(AOS_TEST_APPS
      test_core
      test_time
      test_task
      test_task_delay
      test_mutex
      test_sem
    )
    foreach(_aos_test IN LISTS AOS_TEST_APPS)
      nuttx_add_application(
        NAME      ${_aos_test}
        SRCS      "${AOS_ROOT}/tests/${_aos_test}.c"
        STACKSIZE ${CONFIG_AOS_OSAL_TESTS_STACKSIZE}
        PRIORITY  100
        DEPENDS   aos_osal)
    endforeach()

    # Scratch probe (raw pthreads), removed once the mutex question is settled.
    if(EXISTS "${AOS_ROOT}/tests/nuttx/probe_mutex.c")
      nuttx_add_application(NAME probe_mutex SRCS "${AOS_ROOT}/tests/nuttx/probe_mutex.c"
                            STACKSIZE 16384 PRIORITY 100)
    endif()

    # Bench: shared stats header lives in tests/bench.
    foreach(_aos_bench IN ITEMS test_bench_stats bench_task_lifecycle)
      nuttx_add_application(
        NAME      ${_aos_bench}
        SRCS      "${AOS_ROOT}/tests/bench/${_aos_bench}.c"
        INCLUDE_DIRECTORIES "${AOS_ROOT}/tests/bench"
        STACKSIZE ${CONFIG_AOS_OSAL_TESTS_STACKSIZE}
        PRIORITY  100
        DEPENDS   aos_osal)
    endforeach()
  endif()

endif()
