# Afligh8_osal source lists, shared by the host CMake build (CMakeLists.txt)
# and the NuttX apps glue (nuttx/CMakeLists.txt). Paths are relative to the
# repository root; callers prefix ${AOS_ROOT}.
#
# The implementation is one pthread-family core plus a thin per-OS port:
#   src/os/pthread/     shared: tasks, mutexes, semaphores, init, time, status
#   src/os/<port>/      port: aos_port.h knobs + aos_<port>.c (scheduling, caps)

set(AOS_PTHREAD_SOURCES
    src/os/pthread/src/aos_init.c
    src/os/pthread/src/aos_time.c
    src/os/pthread/src/aos_status.c
    src/os/pthread/src/aos_task.c
    src/os/pthread/src/aos_mutex.c
    src/os/pthread/src/aos_sem.c
)

function(aos_backend_sources backend out_var)
    set(${out_var} ${AOS_PTHREAD_SOURCES} src/os/${backend}/src/aos_${backend}.c PARENT_SCOPE)
endfunction()

function(aos_backend_private_includes backend out_var)
    set(${out_var} src/os/pthread/include src/os/${backend}/include PARENT_SCOPE)
endfunction()
