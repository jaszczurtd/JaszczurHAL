#!/usr/bin/env bash
# =============================================================================
# runalltests.sh
#
# Run the full JaszczurHAL quality-gate suite locally - same checks as CI.
# Safe to re-run. Exits non-zero on the first failure.
#
# Gates (in order):
#   1. Tool presence check
#   2. Host unit tests (cmake + ctest)
#   3. Memory safety (Valgrind memcheck)
#   4. Static analysis: cppcheck (all own code)
#   5. Static analysis: clang-tidy (host + stm32 compile databases)
#   6. STM32 host-compiler library build
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

# ── Args ─────────────────────────────────────────────────────────────────────
JOBS="$(nproc 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs) JOBS="$2"; shift 2 ;;
        -j*)       JOBS="${1#-j}"; shift ;;
        -h|--help)
            head -22 "$0" | tail -19
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
header "Gate 1/6: Checking required tools"

REQUIRED_TOOLS=(cmake g++ gcc make valgrind clang-tidy cppcheck run-clang-tidy)
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

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 2: Host unit tests
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 2/6: Host unit tests (build + ctest)"

BUILD_DIR="${SCRIPT_DIR}/build_test"
rm -rf "${BUILD_DIR}"

info "Configuring..."
cmake -S . -B "${BUILD_DIR}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null 2>&1

info "Building with ${JOBS} jobs..."
cmake --build "${BUILD_DIR}" --parallel "${JOBS}" >/dev/null 2>&1

info "Running tests..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

pass "All unit tests passed."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 3: Valgrind memcheck
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 3/6: Memory safety (Valgrind memcheck)"

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
header "Gate 4/6: Static analysis - cppcheck"

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
header "Gate 5/6: Static analysis - clang-tidy"

# Generate STM32 compile database
BUILD_STM32="${SCRIPT_DIR}/build_stm32_host"
rm -rf "${BUILD_STM32}"
info "Generating STM32 compile database..."
cmake -S stm32_lib -B "${BUILD_STM32}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null 2>&1
cmake --build "${BUILD_STM32}" --parallel "${JOBS}" >/dev/null 2>&1

info "Running clang-tidy on host-compilable code..."
run-clang-tidy -p "${BUILD_DIR}" -quiet \
    '^.*/src/(hal/hal_[^/]*|hal/impl/shared/[^/]*|utils/(?!cJSON|unity)[^/]*)\.(cpp|c)$' \
    2>/dev/null | tee /tmp/jh_tidy_host.log

info "Running clang-tidy on STM32 backend..."
run-clang-tidy -p "${BUILD_STM32}" -quiet \
    '^.*/src/hal/impl/stm32g474/.*\.(cpp|c)$' \
    2>/dev/null | tee /tmp/jh_tidy_stm32.log

if grep -qE ' (warning|error):' /tmp/jh_tidy_host.log /tmp/jh_tidy_stm32.log 2>/dev/null; then
    fail "clang-tidy reported findings:"
    grep -E ' (warning|error):' /tmp/jh_tidy_host.log /tmp/jh_tidy_stm32.log 2>/dev/null | head -20
    exit 1
fi

pass "clang-tidy: no issues found."

# ═══════════════════════════════════════════════════════════════════════════════
# GATE 6: STM32 host-compiler library build
# ═══════════════════════════════════════════════════════════════════════════════
header "Gate 6/6: STM32 host-compiler library build"

info "Building libJaszczurHAL.a (STM32G474 backend, host compiler)..."
# Already built above in gate 5 - just verify artifact exists
if [[ -f "${BUILD_STM32}/libJaszczurHAL.a" ]]; then
    SIZE=$(stat --printf="%s" "${BUILD_STM32}/libJaszczurHAL.a" 2>/dev/null || stat -f "%z" "${BUILD_STM32}/libJaszczurHAL.a" 2>/dev/null || echo "?")
    pass "libJaszczurHAL.a built successfully (${SIZE} bytes)"
else
    fail "libJaszczurHAL.a not found!"
    exit 1
fi

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
echo "  STM32 build:      PASS"
echo ""
echo "  Total time: ${SECONDS}s"
echo ""
