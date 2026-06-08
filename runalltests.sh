#!/usr/bin/env bash
# =============================================================================
# runalltests.sh
#
# Run the full JaszczurHAL quality-gate suite locally, including CI checks.
# Safe to re-run. Exits non-zero on the first failure.
#
# Gates (in order):
#   1. Tool presence check
#   2. Host unit tests (cmake + ctest)
#   3. Memory safety (Valgrind memcheck)
#   4. Static analysis: cppcheck (all own code)
#   5. Static analysis: clang-tidy (host + stm32 compile databases)
#   6. Target static-library builds (STM32 + RP2040)
#   7. Examples build (RP2040 + STM32G474, via examples/CMakeLists.txt)
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

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 1: Tool presence check
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 1/7: Checking required tools"

REQUIRED_TOOLS=(
    cmake g++ gcc make
    valgrind clang-tidy cppcheck run-clang-tidy
    arduino-cli
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

rp2040_core="$(arduino-cli core list 2>/dev/null | grep -m1 -E '^rp2040:rp2040([[:space:]]|$)' || true)"
if [[ -z "${rp2040_core}" ]]; then
    fail "Arduino RP2040 core is not installed. Run ./runmefirst.sh first."
    exit 1
fi
printf '  %-20s %s\n' "rp2040:rp2040" "${rp2040_core}"
pass "Arduino RP2040 core present."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 2: Host unit tests
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 2/7: Host unit tests (build + ctest)"

BUILD_DIR="${SCRIPT_DIR}/build_test"
rm -rf "${BUILD_DIR}"

info "Configuring..."
cmake -S . -B "${BUILD_DIR}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

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
    test_max6675_driver
    test_mcp9600_driver
    test_ads1x15_driver
    test_ili9341_driver
    test_st77xx_driver
    test_ssd1306_driver
    test_jh_gfx_geometry
    test_mcp2515_driver
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
ctest --test-dir "${BUILD_DIR}" -T memcheck --output-on-failure 2>&1 \
    | tee /tmp/jh_memcheck.log | grep -E '(^[0-9]|Memory|passed|failed|Defects)'

# Check for defects in the memcheck output
if grep -q "Memory checking results:" /tmp/jh_memcheck.log; then
    defects=$(grep "Memory checking results:" /tmp/jh_memcheck.log | grep -oP '\d+ defect' | head -1 || true)
    if [[ -n "$defects" && "$defects" != "0 defect" ]]; then
        fail "Valgrind found memory defects!"
        grep -A5 "Memory checking results:" /tmp/jh_memcheck.log
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
    -i src/hal/impl/arduino/drivers \
    -i src/hal/impl/arduino/frameworks \
    -i src/utils/cJSON.c -i src/utils/cJSON_Utils.c -i src/utils/unity.c \
    --error-exitcode=1 --quiet \
    src

pass "cppcheck: no issues found."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 5: clang-tidy (host + stm32)
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 5/7: Static analysis - clang-tidy"

# Generate STM32 compile database
BUILD_STM32="${SCRIPT_DIR}/build_stm32_host"
rm -rf "${BUILD_STM32}"
info "Generating STM32 compile database..."
cmake -S stm32_lib -B "${BUILD_STM32}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
info "Building STM32 compile database..."
cmake --build "${BUILD_STM32}" --parallel "${JOBS}"
pass "STM32 compile database ready."

info "Running clang-tidy on host-compilable code..."
run-clang-tidy -p "${BUILD_DIR}" -quiet \
    '^.*/src/(hal/hal_[^/]*|hal/impl/shared/.*|utils/(?!cJSON|unity)[^/]*)\.(cpp|c)$' \
    | tee /tmp/jh_tidy_host.log
pass "clang-tidy host pass complete."

info "Running clang-tidy on STM32 backend..."
run-clang-tidy -p "${BUILD_STM32}" -quiet \
    '^.*/src/hal/impl/stm32g474/.*\.(cpp|c)$' \
    | tee /tmp/jh_tidy_stm32.log
pass "clang-tidy STM32 pass complete."

if grep -qE ':[0-9]+:[0-9]+: (warning|error):' /tmp/jh_tidy_host.log /tmp/jh_tidy_stm32.log 2>/dev/null; then
    fail "clang-tidy reported findings:"
    grep -E ':[0-9]+:[0-9]+: (warning|error):' /tmp/jh_tidy_host.log /tmp/jh_tidy_stm32.log 2>/dev/null | head -20
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

BUILD_RP2040="${SCRIPT_DIR}/build_arduino"
info "Building libJaszczurHAL.a (RP2040 Arduino backend)..."
run_logged /tmp/jh_rp2040_lib_build.log \
    "${SCRIPT_DIR}/build_arduino_lib.sh" --clean --jobs "${JOBS}"

if [[ -f "${BUILD_RP2040}/libJaszczurHAL.a" ]]; then
    SIZE=$(stat --printf="%s" "${BUILD_RP2040}/libJaszczurHAL.a" 2>/dev/null || stat -f "%z" "${BUILD_RP2040}/libJaszczurHAL.a" 2>/dev/null || echo "?")
    pass "RP2040 libJaszczurHAL.a built successfully (${SIZE} bytes)"
else
    fail "RP2040 libJaszczurHAL.a not found!"
    exit 1
fi

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 7: Examples build
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 7/7: Examples build (RP2040 + STM32G474)"

BUILD_EXAMPLES_RP2040="${SCRIPT_DIR}/build_examples_rp2040"
BUILD_EXAMPLES_STM32="${SCRIPT_DIR}/build_examples_stm32g474"
rm -rf "${BUILD_EXAMPLES_RP2040}" "${BUILD_EXAMPLES_STM32}"

info "Configuring RP2040 examples..."
run_logged /tmp/jh_examples_rp2040_configure.log \
    cmake -S examples -B "${BUILD_EXAMPLES_RP2040}" -DJH_EXAMPLE_TARGET=rp2040

info "Building RP2040 examples supported by examples/CMakeLists.txt..."
run_logged /tmp/jh_examples_rp2040_build.log \
    cmake --build "${BUILD_EXAMPLES_RP2040}" --parallel "${JOBS}"
pass "RP2040 examples built successfully."

info "Configuring STM32G474 examples..."
run_logged /tmp/jh_examples_stm32g474_configure.log \
    cmake -S examples -B "${BUILD_EXAMPLES_STM32}" \
        -DJH_EXAMPLE_TARGET=stm32g474 \
        -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/stm32_lib/toolchain_stm32g474.cmake"

info "Building STM32G474 examples supported by examples/CMakeLists.txt..."
run_logged /tmp/jh_examples_stm32g474_build.log \
    cmake --build "${BUILD_EXAMPLES_STM32}" --parallel "${JOBS}"
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
echo "  Valgrind:         PASS"
echo "  cppcheck:         PASS"
echo "  clang-tidy:       PASS"
echo "  Target builds:    PASS (RP2040 + STM32G474)"
echo "  Examples builds:  PASS (RP2040 + STM32G474)"
echo ""
echo "  Total time: ${SECONDS}s"
echo ""
