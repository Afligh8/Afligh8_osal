# nuttx/ -- building Afligh8_osal inside a NuttX tree

The NuttX backend (`src/os/nuttx/`) is a thin port over the same
pthread-family core the Linux backend uses. It is compiled by NuttX's own
build, not by the host `CMakeLists.txt`, because it needs the kernel's
headers and `.config`.

Two ways in:

- **Standalone, on the simulator:** `tests/nuttx/run_sim.sh` fetches nothing
  itself; point it at a NuttX 13.x checkout (`NUTTX_DIR`, `NUTTX_APPS_DIR`),
  it symlinks `<apps>/external` to this directory, merges
  `src/os/nuttx/aos_osal.defconfig` plus `CONFIG_AOS_OSAL_TESTS=y` into the
  `sim:nsh` configuration, builds, and runs every test as an NSH command.
- **From an application:** its `apps/CMakeLists.txt` adds this directory
  with `add_subdirectory(<path>/nuttx aos_osal)` and its `Kconfig` sources
  `<path>/nuttx/Kconfig`. Set `CONFIG_AOS_OSAL=y` and link `aos_osal`.

Kernel options the backend requires are listed and enforced in
`src/os/nuttx/include/aos_port.h`; `src/os/nuttx/aos_osal.defconfig` is the
fragment to merge into a board's defconfig.
