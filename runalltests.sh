#!/usr/bin/env bash
# =============================================================================
# runalltests.sh
#
# Run the full JaszczurHAL quality-gate suite locally, including CI checks.
# Safe to re-run. Exits non-zero on the first failure.
#
# Gates (in order):
#   1. Tool presence check
#   2. Host unit tests (cmake + ctest, including FreeRTOS POSIX)
#   3. Memory safety (Valgrind memcheck)
#   4. Static analysis: cppcheck (all own code)
#   5. Static analysis: clang-tidy (host + stm32 compile databases)
#   6. Target builds (STM32 + native Pico SDK matrix)
#   7. Examples build (native RP + STM32G474)
#
# Usage:
#   ./runalltests.sh          # run everything
#   ./runalltests.sh -j8      # override parallel jobs
#   ./runalltests.sh --help   # show help
#
# Prerequisites:
#   Run ./runmefirst.sh once to install all required tooling.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="${SCRIPT_DIR}/.build"
GATE_BUILD_ROOT="${BUILD_ROOT}/gate"
LOG_ROOT="${GATE_BUILD_ROOT}/logs"
export PYTHONPYCACHEPREFIX="${BUILD_ROOT}/python-cache"
cd "${SCRIPT_DIR}"

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
pass()  { echo -e "${GREEN}[PASS]${NC} $*"; }
fail()  { echo -e "${RED}[FAIL]${NC} $*"; }
header(){ echo -e "\n${BOLD}══════════════════════════════════════════════════════════════${NC}"; echo -e "${BOLD}  $*${NC}"; echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"; }

run_logged() {
    local log_file="$1"
    shift

    if ! "$@" 2>&1 | tee "${log_file}"; then
        fail "Command failed: $*"
        if [[ -s "${log_file}" ]]; then
            echo ""
            tail -80 "${log_file}"
        fi
        exit 1
    fi
}

clean_build_artifacts() {
    local cleaned=0
    local build_dirs=(
        "${GATE_BUILD_ROOT}"
        "${BUILD_ROOT}/examples"
        "${BUILD_ROOT}/python-cache"
        "${BUILD_ROOT}/tests"
    )

    local candidate
    for candidate in "${build_dirs[@]}"; do
        [[ -d "${candidate}" ]] || continue
        case "${candidate}" in
            "${GATE_BUILD_ROOT}"|"${BUILD_ROOT}/examples"|"${BUILD_ROOT}/python-cache"|"${BUILD_ROOT}/tests") ;;
            *) continue ;;
        esac

        rm -rf -- "${candidate}"
        cleaned=$((cleaned + 1))
    done

    if [[ "${cleaned}" -eq 0 ]]; then
        info "No existing build artifact directories to remove."
    else
        info "Removed ${cleaned} build artifact directories."
    fi
    mkdir -p "${LOG_ROOT}"
}

# ── Args ─────────────────────────────────────────────────────────────────────
JOBS="$(nproc 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs) JOBS="$2"; shift 2 ;;
        -j*)       JOBS="${1#-j}"; shift ;;
        -h|--help)
            awk 'NR >= 4 { if ($0 ~ /^# =/) exit; print }' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Track timing ─────────────────────────────────────────────────────────────
SECONDS=0

# ── Clean start ──────────────────────────────────────────────────────────────
header "Clean start: removing build artifacts"
clean_build_artifacts

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 1: Tool presence check
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 1/7: Checking required tools"

REQUIRED_TOOLS=(
    cmake g++ gcc make
    valgrind clang-tidy cppcheck run-clang-tidy
    arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-ar arm-none-eabi-ranlib arm-none-eabi-objcopy
)
missing=0
for tool in "${REQUIRED_TOOLS[@]}"; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '  %-20s %s\n' "$tool" "$(command -v "$tool")"
    else
        fail "MISSING: $tool"
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    echo ""
    fail "Some required tools are missing. Run ./runmefirst.sh to install them."
    exit 1
fi
pass "All required tools present."

info "Verifying pinned third-party components..."
"${SCRIPT_DIR}/third_party/update_components.sh" --verify-only
pass "Pinned third-party components are ready."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 2: Host unit tests
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 2/7: Host unit tests (build + ctest + FreeRTOS POSIX)"

BUILD_DIR="${GATE_BUILD_ROOT}/host"
rm -rf "${BUILD_DIR}"

info "Configuring..."
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DJH_ENABLE_FREERTOS_POSIX_TESTS=ON

info "Building with ${JOBS} jobs..."
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

info "Running tests..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

pass "All unit tests passed."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 3: Valgrind memcheck
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 3/7: Memory safety (Valgrind memcheck)"

MEMCHECK_REQUIRED_TESTS=(
    test_lwip_raw_engines
    test_network_board_runtime_absent
    test_network_board_runtime_picow
    test_network_board_runtime_pim730
    test_pubsub_hal_client
    test_wireguard_lwip_port
    test_max6675_driver
    test_mcp9600_driver
    test_ads1x15_driver
    test_ili9341_driver
    test_st77xx_driver
    test_ssd1306_driver
    test_jh_gfx_geometry
    test_mcp2515_driver
    test_ff16_memdisk
    test_hal_serial
    test_hal_ds18b20
    test_hal_display
    test_hal_rtc
)

info "Verifying shared-module test coverage in CTest..."
ctest_tests=$(ctest --test-dir "${BUILD_DIR}" -N 2>/dev/null || true)
for test_name in "${MEMCHECK_REQUIRED_TESTS[@]}"; do
    if ! grep -q "${test_name}" <<<"${ctest_tests}"; then
        fail "Missing CTest registration for ${test_name}"
        exit 1
    fi
done

info "Running tests under Valgrind..."
ctest --test-dir "${BUILD_DIR}" -T memcheck -LE no_memcheck --output-on-failure 2>&1 \
    | tee "${LOG_ROOT}/jh_memcheck.log" \
    | grep -E '(^[0-9]|Memory|passed|failed|Defects)'

# Check for defects in the memcheck output
if grep -q "Memory checking results:" "${LOG_ROOT}/jh_memcheck.log"; then
    defects=$(grep "Memory checking results:" "${LOG_ROOT}/jh_memcheck.log" | grep -oP '\d+ defect' | head -1 || true)
    if [[ -n "$defects" && "$defects" != "0 defect" ]]; then
        fail "Valgrind found memory defects!"
        grep -A5 "Memory checking results:" "${LOG_ROOT}/jh_memcheck.log"
        exit 1
    fi
fi

pass "No memory defects found."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 4: cppcheck (all own code)
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 4/7: Static analysis - cppcheck"

info "Scanning src/ (vendored code excluded)..."
cppcheck --enable=warning,performance,portability \
    --inline-suppr \
    --suppressions-list=tests/cppcheck-suppressions.txt \
    -i src/hal/impl/rp2040/drivers \
    -i src/hal/impl/rp2040/frameworks \
    -i src/hal/impl/shared/frameworks/cjson \
    -i src/hal/impl/shared/frameworks/jpeg \
    -i src/hal/impl/shared/frameworks/lodepng \
    -i src/utils/unity.c \
    --error-exitcode=1 --quiet \
    src

pass "cppcheck: no issues found."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 5: clang-tidy (host + stm32)
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 5/7: Static analysis - clang-tidy"

# Generate STM32 compile database
BUILD_STM32="${GATE_BUILD_ROOT}/stm32-host"
rm -rf "${BUILD_STM32}"
info "Generating host-compiler STM32 sanity build..."
cmake -S stm32_lib -B "${BUILD_STM32}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
info "Building host-compiler STM32 sanity library..."
cmake --build "${BUILD_STM32}" --parallel "${JOBS}"
pass "Host-compiler STM32 sanity library ready."

BUILD_STM32_TARGET="${GATE_BUILD_ROOT}/stm32-target"
rm -rf "${BUILD_STM32_TARGET}"
info "Generating real ARM STM32 compile database..."
cmake -S stm32_lib -B "${BUILD_STM32_TARGET}" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/stm32_lib/toolchain_stm32g474.cmake" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
info "Building real ARM STM32 static library..."
cmake --build "${BUILD_STM32_TARGET}" --parallel "${JOBS}"
pass "ARM STM32 compile database ready."

info "Running clang-tidy on host-compilable code..."
TIDY_HOST_BUILD="${BUILD_DIR}/clang_tidy_db"
mapfile -t TIDY_HOST_FILES < <(
    scripts/clang_tidy_files.py --build-dir "${BUILD_DIR}" --repo-root "${SCRIPT_DIR}" --profile host \
        --output-compile-db "${TIDY_HOST_BUILD}/compile_commands.json"
)
if [[ "${#TIDY_HOST_FILES[@]}" -eq 0 ]]; then
    fail "clang-tidy host file list is empty"
    exit 1
fi
run-clang-tidy -p "${TIDY_HOST_BUILD}" -quiet "${TIDY_HOST_FILES[@]}" \
    | tee "${LOG_ROOT}/jh_tidy_host.log"
pass "clang-tidy host pass complete."

info "Running clang-tidy on STM32 backend..."
TIDY_STM32_BUILD="${BUILD_STM32_TARGET}/clang_tidy_db"
mapfile -t TIDY_STM32_FILES < <(
    scripts/clang_tidy_files.py --build-dir "${BUILD_STM32_TARGET}" --repo-root "${SCRIPT_DIR}" --profile stm32 \
        --output-compile-db "${TIDY_STM32_BUILD}/compile_commands.json"
)
if [[ "${#TIDY_STM32_FILES[@]}" -eq 0 ]]; then
    fail "clang-tidy STM32 file list is empty"
    exit 1
fi
run-clang-tidy -p "${TIDY_STM32_BUILD}" -quiet "${TIDY_STM32_FILES[@]}" \
    | tee "${LOG_ROOT}/jh_tidy_stm32.log"
pass "clang-tidy STM32 pass complete."

if grep -qE ':[0-9]+:[0-9]+: (warning|error):' \
        "${LOG_ROOT}/jh_tidy_host.log" "${LOG_ROOT}/jh_tidy_stm32.log" \
        2>/dev/null; then
    fail "clang-tidy reported findings:"
    grep -E ':[0-9]+:[0-9]+: (warning|error):' \
        "${LOG_ROOT}/jh_tidy_host.log" "${LOG_ROOT}/jh_tidy_stm32.log" \
        2>/dev/null | head -20
    exit 1
fi

pass "clang-tidy: no issues found."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 6: Target static-library builds
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 6/7: Target static-library builds"

info "Verifying libJaszczurHAL.a (STM32G474 backend, host compiler)..."
# Already built above in gate 5 - just verify artifact exists
if [[ -f "${BUILD_STM32}/libJaszczurHAL.a" ]]; then
    SIZE=$(stat --printf="%s" "${BUILD_STM32}/libJaszczurHAL.a" 2>/dev/null || stat -f "%z" "${BUILD_STM32}/libJaszczurHAL.a" 2>/dev/null || echo "?")
    pass "libJaszczurHAL.a built successfully (${SIZE} bytes)"
else
    fail "libJaszczurHAL.a not found!"
    exit 1
fi

info "Building native Pico SDK artifact matrix..."
NATIVE_RP_PLATFORMS=(
    rp2040
    rp2350-arm-s
    rp2350-riscv
)
for platform in "${NATIVE_RP_PLATFORMS[@]}"; do
    info "Building native Pico SDK platform: ${platform}"
    run_logged "${LOG_ROOT}/jh_rp_native_${platform}.log" \
        "${SCRIPT_DIR}/scripts/build_rp_native_lib.sh" \
            --platform "${platform}" --example 01_blink \
            --clean --jobs "${JOBS}" \
            --output "${GATE_BUILD_ROOT}/rp-native/${platform}/bare"
done
pass "Native Pico SDK matrix built HAL entry/core probes and 01_blink artifacts successfully."

info "Building native FreeRTOS SMP artifact matrix..."
for platform in "${NATIVE_RP_PLATFORMS[@]}"; do
    info "Building native FreeRTOS SMP platform: ${platform}"
    run_logged "${LOG_ROOT}/jh_rp_native_freertos_${platform}.log" \
        "${SCRIPT_DIR}/scripts/build_rp_native_lib.sh" \
            --platform "${platform}" --example 29_freertos_smoke \
            --freertos --clean --jobs "${JOBS}" \
            --output "${GATE_BUILD_ROOT}/rp-native/${platform}/freertos"
done
pass "Native FreeRTOS SMP matrix built for RP2040 and both RP2350 ISAs."

info "Building RP2040 flag matrix..."
RP_FLAG_PROFILES=(
    empty-core
    no-radio-network
    typical-set
    udp-wireguard
    pim730-owned
    sdlogger
    all-enabled
)
for profile in "${RP_FLAG_PROFILES[@]}"; do
    flags=()
    board=pico
    case "${profile}" in
        empty-core)
            ;;
        no-radio-network)
            # Compile the complete public transport surface for a Pico profile
            # without CYW43. Runtime preflight must reject hardware access.
            flags=(
                -D HAL_ENABLE_WIFI
                -D HAL_ENABLE_TCP
                -D HAL_ENABLE_UDP
                -D HAL_NETWORK_BACKEND_CYW43
                -D HAL_CYW43_BUS_PICO_PIO
                -D HAL_CYW43_STACK_LWIP
                -D HAL_BOARD_PROFILE_RP_PICO
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            )
            ;;
        typical-set)
            board=picow
            flags=(
                -D HAL_ENABLE_WIFI
                -D HAL_ENABLE_MQTT
                -D HAL_ENABLE_KV
                -D HAL_ENABLE_PCF8563
                -D HAL_ENABLE_MCP9600
                -D HAL_ENABLE_DS18B20
                -D HAL_ENABLE_GPS
                -D HAL_ENABLE_ILI9341
                -D HAL_DISPLAY_ILI9341
                -D HAL_ENABLE_PWM_FREQ
                -D HAL_NETWORK_BACKEND_CYW43
                -D HAL_CYW43_BUS_PICO_PIO
                -D HAL_CYW43_STACK_LWIP
                -D HAL_BOARD_PROFILE_RP_PICO_W
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            )
            ;;
        udp-wireguard)
            board=picow
            # Keep UDP independent from TCP. This catches shared network
            # helpers accidentally guarded by HAL_ENABLE_TCP and compiles the
            # bundled WireGuard/lwIP headers with the JaszczurHAL CYW43 stack.
            flags=(
                -D HAL_ENABLE_UDP
                -D HAL_ENABLE_WIREGUARD
                -D HAL_NETWORK_BACKEND_CYW43
                -D HAL_CYW43_BUS_PICO_PIO
                -D HAL_CYW43_STACK_LWIP
                -D HAL_BOARD_PROFILE_RP_PICO_W
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            )
            ;;
        pim730-owned)
            board=pico-rm2
            flags=(
                -D HAL_ENABLE_WIFI
                -D HAL_ENABLE_TCP
                -D HAL_ENABLE_UDP
                -D HAL_NETWORK_BACKEND_CYW43
                -D HAL_CYW43_BUS_PICO_PIO
                -D HAL_CYW43_STACK_LWIP
                -D HAL_BOARD_PROFILE_RP_PICO_PIM730
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            )
            ;;
        sdlogger)
            flags=(
                -D HAL_ENABLE_SDLOGGER
            )
            ;;
        all-enabled)
            board=picow
            flags=(
                -D HAL_ENABLE_WIFI
                -D HAL_ENABLE_TIME
                -D HAL_ENABLE_MQTT
                -D HAL_ENABLE_UDP
                -D HAL_ENABLE_TCP
                -D HAL_ENABLE_BSD_SOCKETS
                -D HAL_ENABLE_TLS
                -D HAL_ENABLE_HTTP_CLIENT
                -D HAL_ENABLE_HTTP_SERVER
                -D HAL_ENABLE_HTTP_FILES
                -D HAL_ENABLE_WEBSOCKET
                -D HAL_ENABLE_NET_CONSOLE
                -D HAL_ENABLE_NET_COMMANDS
                -D HAL_ENABLE_OTA
                -D HAL_ENABLE_WIREGUARD
                -D HAL_ENABLE_EEPROM
                -D HAL_ENABLE_KV
                -D HAL_ENABLE_LITTLEFS
                -D HAL_ENABLE_FAT
                -D HAL_ENABLE_SDLOGGER
                -D HAL_ENABLE_UART
                -D HAL_ENABLE_SWSERIAL
                -D HAL_ENABLE_I2C
                -D HAL_ENABLE_I2C_SLAVE
                -D HAL_ENABLE_MCP2515
                -D HAL_ENABLE_PCF8563
                -D HAL_ENABLE_DS3231
                -D HAL_ENABLE_MCP9600
                -D HAL_ENABLE_MAX6675
                -D HAL_ENABLE_DS18B20
                -D HAL_ENABLE_ONEWIRE
                -D HAL_ENABLE_EXTERNAL_ADC
                -D HAL_ENABLE_GPS
                -D HAL_ENABLE_DAC
                -D HAL_ENABLE_PCNT
                -D HAL_ENABLE_PWM_FREQ
                -D HAL_ENABLE_RGB_LED
                -D HAL_ENABLE_ILI9341
                -D HAL_DISPLAY_ILI9341
                -D HAL_ENABLE_SSD1306
                -D HAL_ENABLE_CRYPTO
                -D HAL_ENABLE_CJSON
                -D HAL_NETWORK_BACKEND_CYW43
                -D HAL_CYW43_BUS_PICO_PIO
                -D HAL_CYW43_STACK_LWIP
                -D HAL_BOARD_PROFILE_RP_PICO_W
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            )
            ;;
    esac

    matrix_build_dir="${GATE_BUILD_ROOT}/rp-native-flags/${profile}"
    info "Building RP2040 flag profile: ${profile}"
    run_logged "${LOG_ROOT}/jh_rp_native_${profile}.log" \
        "${SCRIPT_DIR}/scripts/build_rp_native_lib.sh" \
            --platform rp2040 --board "${board}" --clean --jobs "${JOBS}" \
            --output "${matrix_build_dir}" "${flags[@]}"

    if [[ ! -f "${matrix_build_dir}/libJaszczurHAL.a" ]]; then
        fail "RP2040 flag profile ${profile} did not produce libJaszczurHAL.a"
        exit 1
    fi
done
pass "RP2040 flag matrix built successfully."

info "Verifying native RP link inputs..."
if find "${GATE_BUILD_ROOT}/rp-native" "${GATE_BUILD_ROOT}/rp-native-flags" \
        -type f -name core.a -print -quit | grep -q .; then
    fail "Native RP build produced an unexpected core.a"
    exit 1
fi
if grep -R -E '(^|[ /])core\.a([[:space:]]|$)|arduino-pico|Arduino\.h' \
        "${GATE_BUILD_ROOT}/rp-native" "${GATE_BUILD_ROOT}/rp-native-flags" \
        --include='link.txt' --include='*.map' --include='compile_commands.json' \
        --include='CMakeCache.txt'; then
    fail "Native RP build metadata references the removed carrier"
    exit 1
fi
pass "Native RP images contain no removed carrier link inputs."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 7: Examples build
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 7/7: Examples build (native RP + STM32G474)"

for target in rp2040 rp2350-arm rp2350-riscv; do
    info "Building every declared native VS Code example for ${target}..."
    run_logged "${LOG_ROOT}/jh_examples_${target}_native_build.log" \
        "${SCRIPT_DIR}/scripts/examples_dispatcher.py" build \
            --target "${target}" --jobs "${JOBS}"
done
pass "Complete native RP example matrix built successfully."

info "Building native RP USB-multicore and SDLogger parity fixtures..."
run_logged "${LOG_ROOT}/jh_rp_native_parity_fixtures.log" \
    "${SCRIPT_DIR}/scripts/build_rp_native_parity_fixtures.sh" \
        --jobs "${JOBS}"
pass "Native RP parity fixtures built for all target/runtime combinations."

info "Building STM32G474 examples through dispatcher-backed VS Code manifests..."
run_logged "${LOG_ROOT}/jh_examples_stm32g474_build.log" \
    "${SCRIPT_DIR}/scripts/examples_dispatcher.py" build --target stm32g474 --jobs "${JOBS}"
pass "STM32G474 examples built successfully."

# ═══════════════════════════════════════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════════════════════════════════════
echo ""
echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}${BOLD}  ALL GATES PASSED ✓${NC}"
echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo ""
echo "  Unit tests:       PASS"
echo "  FreeRTOS POSIX:   PASS"
echo "  Valgrind:         PASS"
echo "  cppcheck:         PASS"
echo "  clang-tidy:       PASS"
echo "  Target builds:    PASS (native Pico SDK + STM32G474)"
echo "  Examples builds:  PASS (native RP + STM32G474)"
echo ""
echo "  Total time: ${SECONDS}s"
echo ""
