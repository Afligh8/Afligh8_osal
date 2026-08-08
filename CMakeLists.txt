cmake_minimum_required(VERSION 3.20)
project(aos_osal LANGUAGES C)   # ++ add CXX here when you consume AOS from AF8's C++ code

#  Backend selection:  -DAOS_BACKEND=posix (default) | nuttx
set(AOS_BACKEND "posix" CACHE STRING "OS backend: posix | nuttx")
set_property(CACHE AOS_BACKEND PROPERTY STRINGS posix nuttx)


#  Global build settings
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)
add_compile_definitions(_POSIX_C_SOURCE=200809L)  # exposes clock_gettime/nanosleep under -std=c99
                                                  


set(AOS_SOURCES
    # src/os/${AOS_BACKEND}/src/aos_${AOS_BACKEND}.c   # posix -> aos_posix.c, nuttx -> aos_nuttx.c

    src/os/${AOS_BACKEND}/src/aos_posix.c
    src/os/${AOS_BACKEND}/src/aos_status.c

    #   src/os/${AOS_BACKEND}/src/aos_task.c
    #   src/os/${AOS_BACKEND}/src/aos_mutex.c
    #   src/os/${AOS_BACKEND}/src/aos_binsem.c
)

add_library(aos_osal STATIC ${AOS_SOURCES})

target_include_directories(aos_osal PUBLIC
    src/include                      # public header: aos_osal.h

)


find_package(Threads REQUIRED)
target_link_libraries(aos_osal PUBLIC Threads::Threads)

#  Tests  (run with:  ctest --test-dir build --output-on-failure)
enable_testing()

# one line registers a module test living in tests/<name>.c
function(aos_add_test name)
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE aos_osal)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

aos_add_test(test_core)        # M0  
aos_add_test(test_time)        # M1  
aos_add_test(test_task)        # M2  
# ++ aos_add_test(test_mutex)    # M3  — uncomment when tests/test_mutex.c exists
# ++ aos_add_test(test_binsem)   # M4
