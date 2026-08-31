cmake_minimum_required(VERSION 3.20)
project(aos_osal LANGUAGES C)

set(AOS_BACKEND "posix" CACHE STRING "OS backend: posix | nuttx")
set_property(CACHE AOS_BACKEND PROPERTY STRINGS posix nuttx)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
add_compile_definitions(_POSIX_C_SOURCE=200809L)
                                                  
set(AOS_SOURCES
    src/os/${AOS_BACKEND}/src/aos_${AOS_BACKEND}.c
    src/os/${AOS_BACKEND}/src/aos_status.c
    src/os/${AOS_BACKEND}/src/aos_task.c
    src/os/${AOS_BACKEND}/src/aos_mutex.c
    src/os/${AOS_BACKEND}/src/aos_sem.c
)

add_library(aos_osal STATIC ${AOS_SOURCES})

target_include_directories(aos_osal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/include

    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/os/${AOS_BACKEND}/include
)

find_package(Threads REQUIRED)

target_link_libraries(aos_osal PUBLIC Threads::Threads)

# Only build/register this repo's own tests when it's the top-level
# project. When another project pulls this in as a dependency (e.g. via
# FetchContent/add_subdirectory -- CMAKE_PROJECT_NAME is then that outer
# project's name, not "aos_osal"), its ctest output shouldn't get
# polluted with every test binary this OSAL ships internally.
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)

enable_testing()

function(aos_add_test name)
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE aos_osal)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# linux...
if(AOS_BACKEND STREQUAL "posix"
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux")

    aos_add_test(test_mutex_pi_posix)

    set_tests_properties(
        test_mutex_pi_posix
        PROPERTIES
            SKIP_RETURN_CODE 77
    )

    # Separate OSAL build with AOS_CONFIG_POSIX_RT_REQUIRED=1, used only
    # to exercise AOS_PosixConfigureRtPolicy()'s "RT is mandatory" branch
    # (AOS_ERR_PERMISSION_DENIED / AOS_ERR_UNSUPPORTED wiring). The
    # default aos_osal target keeps AOS_CONFIG_POSIX_RT_REQUIRED at its
    # default (0), so that branch would otherwise never be compiled or
    # run by anything in this test suite.
    add_library(aos_osal_rt_required STATIC ${AOS_SOURCES})

    target_include_directories(aos_osal_rt_required
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/os/${AOS_BACKEND}/include
    )

    target_compile_definitions(aos_osal_rt_required PRIVATE AOS_CONFIG_POSIX_RT_REQUIRED=1)
    target_link_libraries(aos_osal_rt_required PUBLIC Threads::Threads)

    add_executable(test_rt_required tests/test_rt_required.c)
    target_link_libraries(test_rt_required PRIVATE aos_osal_rt_required)
    add_test(NAME test_rt_required COMMAND test_rt_required)

endif()
# linux ...

aos_add_test(test_core)
aos_add_test(test_time)
aos_add_test(test_task)
aos_add_test(test_task_delay)
aos_add_test(test_mutex)
aos_add_test(test_sem)

endif() # CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME

