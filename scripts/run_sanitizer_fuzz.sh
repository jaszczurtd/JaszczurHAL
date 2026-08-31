#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/.build/sanitizer-fuzz"
JOBS="$(nproc 2>/dev/null || echo 4)"
FUZZ_RUNS=2000
CHECK_TOOLS=0

usage() {
    cat <<'EOF'
Usage: scripts/run_sanitizer_fuzz.sh [options]

Configure, build, and run the Clang ASan/UBSan host suite and parser fuzz smoke
tests. The build directory must stay below the repository .build/ tree.

Options:
  --build-dir PATH   Build directory (default: .build/sanitizer-fuzz)
  -j, --jobs N       Parallel build jobs
  --fuzz-runs N      Iterations per fuzz target (default: 2000)
  --check-tools      Resolve and print the required Clang tools, then exit
  -h, --help         Show this help

JH_SANITIZER_CC and JH_SANITIZER_CXX may select explicit Clang executables.
EOF
}

require_positive_integer() {
    local name="$1"
    local value="$2"
    if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
        echo "Invalid ${name}: ${value}" >&2
        exit 2
    fi
}

resolve_command() {
    local configured="$1"
    shift
    if [[ -n "${configured}" ]]; then
        if [[ "${configured}" == */* ]]; then
            [[ -x "${configured}" ]] && printf '%s\n' "${configured}" && return 0
        elif command -v "${configured}" >/dev/null 2>&1; then
            command -v "${configured}"
            return 0
        fi
        echo "Configured tool is not executable: ${configured}" >&2
        return 1
    fi

    local candidate
    for candidate in "$@"; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done
    return 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            BUILD_DIR="$2"
            shift 2
            ;;
        -j|--jobs)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            JOBS="$2"
            shift 2
            ;;
        -j*)
            JOBS="${1#-j}"
            shift
            ;;
        --fuzz-runs)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            FUZZ_RUNS="$2"
            shift 2
            ;;
        --check-tools)
            CHECK_TOOLS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

require_positive_integer "job count" "${JOBS}"
require_positive_integer "fuzz run count" "${FUZZ_RUNS}"

for required_tool in cmake ctest realpath; do
    if ! command -v "${required_tool}" >/dev/null 2>&1; then
        echo "Required sanitizer runner tool not found: ${required_tool}" >&2
        exit 1
    fi
done

CLANG_CC="$(resolve_command "${JH_SANITIZER_CC:-}" \
    clang clang-20 clang-19 clang-18 clang-17 clang-16)" || {
    echo "Clang C compiler not found; run ./runmefirst.sh" >&2
    exit 1
}
CLANG_BASENAME="$(basename "${CLANG_CC}")"
CLANG_SUFFIX="${CLANG_BASENAME#clang}"
CLANG_DIR="$(dirname "${CLANG_CC}")"
CLANG_CXX="$(resolve_command "${JH_SANITIZER_CXX:-}" \
    "${CLANG_DIR}/clang++${CLANG_SUFFIX}" "clang++${CLANG_SUFFIX}" \
    clang++ clang++-20 clang++-19 clang++-18 \
    clang++-17 clang++-16)" || {
    echo "Clang C++ compiler matching ${CLANG_CC} not found; run ./runmefirst.sh" >&2
    exit 1
}

if ! "${CLANG_CC}" --version | head -1 | grep -q "clang version"; then
    echo "Sanitizer C compiler is not Clang: ${CLANG_CC}" >&2
    exit 1
fi
if ! "${CLANG_CXX}" --version | head -1 | grep -q "clang version"; then
    echo "Sanitizer C++ compiler is not Clang: ${CLANG_CXX}" >&2
    exit 1
fi

printf '[INFO] Sanitizer C compiler:   %s\n' "${CLANG_CC}"
printf '[INFO] Sanitizer C++ compiler: %s\n' "${CLANG_CXX}"
printf '[INFO] CMake:                   %s\n' "$(command -v cmake)"
printf '[INFO] CTest:                   %s\n' "$(command -v ctest)"
if [[ "${CHECK_TOOLS}" -eq 1 ]]; then
    exit 0
fi

if [[ "${BUILD_DIR}" != /* ]]; then
    BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
fi
BUILD_DIR="$(realpath -m -- "${BUILD_DIR}")"
case "${BUILD_DIR}" in
    "${REPO_ROOT}/.build/"*) ;;
    *)
        echo "Refusing sanitizer build directory outside ${REPO_ROOT}/.build: ${BUILD_DIR}" >&2
        exit 2
        ;;
esac

cmake -E remove_directory "${BUILD_DIR}"
cmake -E env "CC=${CLANG_CC}" "CXX=${CLANG_CXX}" \
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DJH_ENABLE_SANITIZERS=ON \
        -DJH_ENABLE_FUZZING=ON
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

SANITIZER_ENV=(
    "ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1"
    "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
)
cmake -E env "${SANITIZER_ENV[@]}" \
    ctest --test-dir "${BUILD_DIR}" --output-on-failure

for target in fuzz_http_server fuzz_websocket fuzz_http_multipart; do
    cmake -E env "${SANITIZER_ENV[@]}" \
        "${BUILD_DIR}/tests/${target}" \
        "-runs=${FUZZ_RUNS}" -max_len=1024
done

echo "[PASS] Clang ASan/UBSan tests and parser fuzz smoke checks passed."
