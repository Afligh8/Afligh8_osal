#!/usr/bin/env bash
#
# Afligh8_osal -- build and run the test suite on the NuttX simulator.
#
# Usage:
#   NUTTX_DIR=/path/to/nuttx NUTTX_APPS_DIR=/path/to/nuttx-apps tests/nuttx/run_sim.sh [build-dir]
#
# Needs a NuttX 13.x checkout and its apps (this script fetches nothing),
# python kconfiglib, kconfig-tweak, ninja, gcc. It:
#   1. symlinks <apps>/external -> this repo's nuttx/ (the apps glue),
#   2. configures sim:nsh with NuttX's CMake build,
#   3. merges src/os/nuttx/aos_osal.defconfig plus the test options into
#      .config and lets Kconfig resolve dependencies,
#   4. builds the simulator,
#   5. runs each test as an NSH command, feeding NSH from stdin, and checks
#      the exit status NSH reports.
#
# Exit status: 0 if every test passed, 1 otherwise.

set -euo pipefail

OSAL_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NUTTX_DIR="${NUTTX_DIR:?set NUTTX_DIR to a NuttX 13.x checkout}"
NUTTX_APPS_DIR="${NUTTX_APPS_DIR:?set NUTTX_APPS_DIR to the matching nuttx-apps checkout}"
BUILD_DIR="${1:-${OSAL_ROOT}/build/nuttx-sim}"

TESTS=(test_core test_time test_task test_task_delay test_mutex test_sem test_bench_stats)

echo "  Afligh8_osal on NuttX sim"
echo "  NuttX     : ${NUTTX_DIR} ($(git -C "${NUTTX_DIR}" describe --tags --always 2>/dev/null || echo '?'))"
echo "  apps      : ${NUTTX_APPS_DIR}"
echo "  build dir : ${BUILD_DIR}"
echo

# 1. apps/external -> our glue
if [[ -e "${NUTTX_APPS_DIR}/external" && ! -L "${NUTTX_APPS_DIR}/external" ]]; then
    echo "ERROR: ${NUTTX_APPS_DIR}/external exists and is not a symlink; refusing to touch it" >&2
    exit 2
fi
ln -sfn "${OSAL_ROOT}/nuttx" "${NUTTX_APPS_DIR}/external"

# 2. configure sim:nsh
if [[ ! -f "${BUILD_DIR}/.config" ]]; then
    cmake -S "${NUTTX_DIR}" -B "${BUILD_DIR}" -DBOARD_CONFIG=sim:nsh \
          -DNUTTX_APPS_DIR="${NUTTX_APPS_DIR}" -GNinja > "${BUILD_DIR}.configure.log" 2>&1 \
        || { tail -30 "${BUILD_DIR}.configure.log"; exit 1; }
fi

# 3. merge our options
TWEAK=(kconfig-tweak --file "${BUILD_DIR}/.config")
while IFS= read -r line; do
    [[ -z "${line}" || "${line}" == \#* ]] && continue
    key="${line%%=*}"; val="${line#*=}"
    case "${val}" in
        y) "${TWEAK[@]}" --enable "${key}" ;;
        n) "${TWEAK[@]}" --disable "${key}" ;;
        \"*) "${TWEAK[@]}" --set-str "${key}" "${val//\"/}" ;;
        *)  "${TWEAK[@]}" --set-val "${key}" "${val}" ;;
    esac
done < "${OSAL_ROOT}/src/os/nuttx/aos_osal.defconfig"
"${TWEAK[@]}" --enable CONFIG_AOS_OSAL
"${TWEAK[@]}" --enable CONFIG_AOS_OSAL_TESTS
# The sim board's default kernel work-queue priority (224) sits inside the
# window the NuttX port now claims (240..209, matched to PX4_WQ_HP_BASE).
# A real board's own defconfig makes this choice for real reasons (ours
# will follow PX4's 249); here it's just so the simulator's guard
# (AOS_CONFIG_NUTTX_PRIORITY_HIGHEST < CONFIG_SCHED_HPWORKPRIORITY) is
# satisfiable with no real interrupt bottom halves to protect.
"${TWEAK[@]}" --set-val CONFIG_SCHED_HPWORKPRIORITY 249
"${TWEAK[@]}" --set-val CONFIG_AOS_OSAL_TESTS_STACKSIZE 65536   # host-sized stacks on the sim
"${TWEAK[@]}" --enable CONFIG_LIBM                              # sqrt() in the bench stats

# Let Kconfig resolve dependencies and pick up the new symbols (re-running
# the configure step runs olddefconfig).
cmake -S "${NUTTX_DIR}" -B "${BUILD_DIR}" > "${BUILD_DIR}.reconfigure.log" 2>&1 \
    || { tail -30 "${BUILD_DIR}.reconfigure.log"; exit 1; }

echo "  merged options:"
grep -E "^CONFIG_(AOS_|PRIORITY_INHERITANCE|PTHREAD_MUTEX_TYPES|CANCELLATION_POINTS|TLS_NELEM|TLS_NCLEANUP|SEM_PREALLOCHOLDERS|LIBM|LIBC_STRERROR)" "${BUILD_DIR}/.config" | sed 's/^/    /'
echo

# 4. build
cmake --build "${BUILD_DIR}" > "${BUILD_DIR}.build.log" 2>&1 \
    || { grep -iE "error|warning: " "${BUILD_DIR}.build.log" | head -40; tail -5 "${BUILD_DIR}.build.log"; exit 1; }
echo "  built ${BUILD_DIR}/nuttx"
echo

# 5. run each test through NSH. NSH echoes "$?" as the last command's status.
script=""
for t in "${TESTS[@]}"; do
    script+="${t}"$'\n'"echo AOS_RESULT ${t} \$?"$'\n'
done
script+="poweroff"$'\n'

output="$(printf '%s' "${script}" | timeout 120 "${BUILD_DIR}/nuttx" 2>&1 || true)"
echo "${output}" | sed 's/^/    /' | tail -80
echo

fails=0
for t in "${TESTS[@]}"; do
    rc="$(echo "${output}" | tr -d '\r' | sed -nE "s/^.*AOS_RESULT ${t} ([0-9]+).*$/\1/p" | tail -1)"
    if [[ "${rc}" == "0" ]]; then
        printf "  [PASS] %s\n" "${t}"
    else
        printf "  [FAIL] %s (status '%s')\n" "${t}" "${rc:-none}"
        fails=$((fails + 1))
    fi
done

echo
if [[ ${fails} -eq 0 ]]; then echo "  ALL ${#TESTS[@]} NUTTX SIM TESTS PASSED"; exit 0; fi
echo "  ${fails} FAILED"; exit 1
