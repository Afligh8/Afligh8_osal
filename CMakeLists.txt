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

endif()
# linux ...

aos_add_test(test_core)
aos_add_test(test_time)
aos_add_test(test_task)
aos_add_test(test_mutex)
aos_add_test(test_sem)

