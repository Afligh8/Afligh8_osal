# Afligh8_osal
Afligh8_osal is a portable OSAL that lets the same embedded and flight-control code run across host and target — Linux/POSIX for SITL and HIL, NuttX on real hardware. Threads, synchronization, timing, and message queues sit behind one stable API with a thin per-backend layer, so bringing up a new RTOS (Zephyr, FreeRTOS, …) means writing one backend, not touching your app.

## Layout

```
src/include/          the API: aos_osal.h, aos_task.h, aos_mutex.h, aos_sem.h, aos_config.h
src/os/pthread/       the implementation shared by every pthread-family OS
src/os/posix/         Linux port: aos_port.h knobs + RT privilege probe + capabilities
src/os/nuttx/         NuttX port: aos_port.h knobs + Kconfig guards + fixed priority window
nuttx/                NuttX apps glue: builds the library (and the tests) inside a NuttX tree
tests/                host test suite (ctest) and tests/bench/ (Phase 6 benchmarks)
tests/nuttx/          run_sim.sh: the same tests on the NuttX simulator
cmake/AosSources.cmake  the one source list both builds use
```

A backend is a *port*, not a copy: `src/os/<port>/include/aos_port.h`
answers the few questions that differ between operating systems (default
stack size, which clock bounded waits use, and for NuttX the priority window
and required kernel options), and `src/os/<port>/src/aos_<port>.c` provides
`AOS_PortConfigureScheduling()` and `AOS_GetCapabilities()`. Everything
else, including the task join/delete races and the lock-free semaphore
post, exists once in `src/os/pthread/`.

## Building

Host (Linux), the default:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

NuttX: the library is compiled by NuttX's own build. See `nuttx/README.md`
for wiring it into an application, and run the suite on the simulator with

```bash
NUTTX_DIR=/path/to/nuttx NUTTX_APPS_DIR=/path/to/nuttx-apps tests/nuttx/run_sim.sh
```

The kernel options the NuttX backend needs are listed in
`src/os/nuttx/include/aos_port.h` and provided as a defconfig fragment in
`src/os/nuttx/aos_osal.defconfig`.
