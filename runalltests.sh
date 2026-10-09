#!/usr/bin/env bash
# =============================================================================
# runalltests.sh
#
# The JaszczurHAL quality gate. STAGES below is the one list of checks: a
# local run executes every stage in order, and the Linux CI jobs run the same
# stages with --stage and nothing else, except the LOCAL_ONLY_STAGES
# (tests/test_ci_gate_stages.py keeps the workflow in step with both lists).
# A local run thus checks everything Linux CI checks on the same commit;
# Windows jobs cover what this host cannot run.
#
# Usage:
#   ./runalltests.sh                    # every stage on the working tree
#   ./runalltests.sh --commit [REV]     # every stage on a clean checkout of
#                                       # REV (default HEAD) under
#                                       # ~/.cache/jaszczurhal; run before a push
#   ./runalltests.sh --stage host       # one stage; more as host,memcheck
#                                       # or by repeating --stage
#   ./runalltests.sh --list-stages      # stage names in run order
#   ./runalltests.sh --list-local-stages  # stages CI does not run
#   ./runalltests.sh --list-libraries   # target:board of the library-* stages
#   ./runalltests.sh -j8                # parallel jobs
#
# Generated files are checked, never rewritten; refresh them with
# python3 scripts/sync_generated.py --write.
#
# Prerequisites: ./runmefirst.sh once (CI: scripts/install_host_tools.sh).
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="${SCRIPT_DIR}/.build"
GATE_BUILD_ROOT="${BUILD_ROOT}/gate"
LOG_ROOT="${GATE_BUILD_ROOT}/logs"
GENERATED_REPORT="${LOG_ROOT}/generated_artifacts.txt"
export PYTHONPYCACHEPREFIX="${BUILD_ROOT}/python-cache"
export PYTHONUTF8=1
cd "${SCRIPT_DIR}"

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
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

# ── Stages ───────────────────────────────────────────────────────────────────
# Target and board of each all-features library stage. The Windows CI matrix
# builds the same pairs through CMake.
LIBRARY_BOARDS=(
    rp2040:picow
    rp2350-arm:pico2w
    rp2350-riscv:pico2
    stm32g474:nucleo-g474re-pim730
)
EXAMPLE_TARGETS=(rp2040 stm32g474 esp32s3)

STAGES=(
    tools repository host sanitizer-fuzz memcheck cppcheck clang-tidy cpd
    stm32 rp esp-idf
)
for entry in "${LIBRARY_BOARDS[@]}"; do
    STAGES+=("library-${entry%%:*}")
done
# The example builds run only locally (user decision, 2026-10-06).
LOCAL_ONLY_STAGES=()
for target in "${EXAMPLE_TARGETS[@]}"; do
    STAGES+=("examples-${target}")
    LOCAL_ONLY_STAGES+=("examples-${target}")
done
STAGES+=(security)

stage_title() {
    case "$1" in
        tools) echo "required tools and pinned components" ;;
        repository) echo "generated files, release metadata, effective features" ;;
        host) echo "host unit tests (CMake + ctest, FreeRTOS POSIX)" ;;
        sanitizer-fuzz) echo "Clang ASan/UBSan, TSan and parser fuzz smoke checks" ;;
        memcheck) echo "memory safety (Valgrind memcheck)" ;;
        cppcheck) echo "static analysis: cppcheck" ;;
        clang-tidy) echo "static analysis: clang-tidy (host + STM32 databases)" ;;
        cpd) echo "duplicate detection: PMD CPD" ;;
        stm32) echo "STM32G474 static libraries" ;;
        rp) echo "native Pico SDK artifacts and RP2040 flag profiles" ;;
        esp-idf) echo "ESP-IDF fixtures and the ESP32-S3 library" ;;
        library-*) echo "${1#library-} all-features libraries, plain and for an application" ;;
        examples-*) echo "every ${1#examples-} example configuration" ;;
        security) echo "SBOM vulnerability scan" ;;
    esac
}

# ── Args ─────────────────────────────────────────────────────────────────────
JOBS="$(nproc 2>/dev/null || echo 4)"
SELECTED=()
COMMIT_MODE=0
COMMIT_REV=HEAD
FORWARDED=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs) JOBS="$2"; FORWARDED+=(-j "$2"); shift 2 ;;
        -j*)       JOBS="${1#-j}"; FORWARDED+=("$1"); shift ;;
        --stage)
            [[ $# -ge 2 ]] || { fail "--stage needs a name (see --list-stages)"; exit 1; }
            IFS=, read -r -a names <<<"$2"
            SELECTED+=("${names[@]}"); FORWARDED+=(--stage "$2"); shift 2 ;;
        --commit)
            COMMIT_MODE=1
            shift
            if [[ $# -gt 0 && "$1" != -* ]]; then
                COMMIT_REV="$1"
                shift
            fi
            ;;
        --list-stages) printf '%s\n' "${STAGES[@]}"; exit 0 ;;
        --list-local-stages) printf '%s\n' "${LOCAL_ONLY_STAGES[@]}"; exit 0 ;;
        --list-libraries) printf '%s\n' "${LIBRARY_BOARDS[@]}"; exit 0 ;;
        -h|--help)
            awk 'NR >= 4 { if ($0 ~ /^# =/) exit; print }' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

for stage in "${SELECTED[@]}"; do
    if ! printf '%s\n' "${STAGES[@]}" | grep -qxF -- "${stage}"; then
        fail "Unknown stage: ${stage} (see --list-stages)"
        exit 1
    fi
done

# ── Clean checkout of a commit ───────────────────────────────────────────────
# Runs the gate on exactly what REV holds: no untracked files, no unstaged
# edits. The checkout lives outside the repository, under the user cache: a
# checkout inside the ignored .build would count as ignored for tools that
# honour parent .gitignore files, so osv-scanner would scan nothing. It is
# reused: checked out again and cleaned of everything but the pinned
# components and built tools. Git
# components are copied from this tree with rsync, as real directories like
# in CI; before any stage runs, whichever were selected, the checkout's
# component verification rejects a local edit in them, submodules included.
# Archive tools (PMD, the RISC-V toolchain) are installed in the checkout from
# their pinned archives instead of copied, once, and kept.
run_on_commit() {
    local sha worktree entry
    local git_components=() archive_components=() excludes=(-e /.build/tools)
    sha="$(git rev-parse --verify "${COMMIT_REV}^{commit}")"
    # One checkout per repository copy.
    worktree="${XDG_CACHE_HOME:-${HOME}/.cache}/jaszczurhal/commit-gate/$(printf '%s' "${SCRIPT_DIR}" | sha256sum | cut -c1-12)"
    while IFS= read -r entry; do
        entry="${entry%/}"
        [[ -d "${entry}" ]] || continue
        excludes+=(-e "/${entry}")
        if [[ -e "${entry}/.git" ]]; then
            git_components+=("${entry}")
        else
            archive_components+=("${entry#third_party/}")
        fi
    done < <(git ls-files --others --ignored --exclude-standard --directory third_party)

    info "Checking out ${COMMIT_REV} (${sha}) in ${worktree}..."
    if git -C "${worktree}" rev-parse --is-inside-work-tree >/dev/null 2>&1 &&
       [[ "$(git -C "${worktree}" rev-parse --show-toplevel)" == "${worktree}" ]]; then
        git -C "${worktree}" checkout --quiet --detach --force "${sha}"
        git -C "${worktree}" clean -ffdxq "${excludes[@]}"
    else
        rm -rf -- "${worktree}"
        git worktree prune
        mkdir -p "$(dirname "${worktree}")"
        git worktree add --detach --quiet "${worktree}" "${sha}"
    fi
    for entry in "${git_components[@]}"; do
        if [[ -L "${worktree}/${entry}" ]]; then
            rm -- "${worktree}/${entry}"
        fi
        mkdir -p "${worktree}/${entry}"
        rsync -a --delete "${SCRIPT_DIR}/${entry}/" "${worktree}/${entry}/"
    done
    for entry in "${archive_components[@]}"; do
        python3 "${worktree}/scripts/component_manager.py" component "${entry}" \
            --enable --repo-root "${worktree}"
    done
    "${worktree}/scripts/ensure_cppcheck.sh"
    "${worktree}/scripts/ensure_picotool.sh" --enable
    # Whatever stages run, they build only from the pinned components: the
    # copies must be clean, submodules included, before any stage starts.
    info "Verifying the pinned components in ${worktree}..."
    "${worktree}/third_party/update_components.sh" --verify-only
    if [[ -d "${worktree}/third_party/esp-idf" ]]; then
        # Only the checkout: ESP-IDF builds install the target tools for this
        # path themselves and verify the checkout again.
        python3 - "${worktree}" <<'VERIFY_ESP_IDF'
import sys
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / "scripts"))
import component_manager
try:
    component_manager.ensure_git_component("esp-idf", Path(sys.argv[1]), verify_only=True)
except component_manager.ComponentError as error:
    sys.exit(f"[ERROR] {error}")
VERIFY_ESP_IDF
    fi
    exec "${worktree}/runalltests.sh" "${FORWARDED[@]}"
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
        rm -rf -- "${candidate}"
        cleaned=$((cleaned + 1))
    done

    if [[ "${cleaned}" -eq 0 ]]; then
        info "No existing build artifact directories to remove."
    else
        info "Removed ${cleaned} build artifact directories."
    fi
}

# ── Shared builds ────────────────────────────────────────────────────────────
HOST_BUILD="${GATE_BUILD_ROOT}/host"
STM32_HOST_BUILD="${GATE_BUILD_ROOT}/stm32-host"
STM32_ARM_BUILD="${GATE_BUILD_ROOT}/stm32-target"
STM32_TOOLCHAIN="${SCRIPT_DIR}/link_libraries/stm32_lib/toolchain_stm32g474.cmake"

# ensure_cmake_build BUILD CONFIGURE_ARGS...: configure BUILD the first time,
# then build it. Stages that share a build call this, so a full run builds it
# once and a stage run alone still gets it.
ensure_cmake_build() {
    local build="$1"
    shift
    if [[ ! -f "${build}/CMakeCache.txt" ]]; then
        cmake -B "${build}" "$@"
    fi
    cmake --build "${build}" --parallel "${JOBS}"
}

ensure_host_build() {
    info "Building the host tests with ${JOBS} jobs..."
    ensure_cmake_build "${HOST_BUILD}" -S . \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DJH_ENABLE_FREERTOS_POSIX_TESTS=ON
}

ensure_stm32_builds() {
    info "Building the host-compiler STM32 sanity library..."
    ensure_cmake_build "${STM32_HOST_BUILD}" -S link_libraries/stm32_lib \
        -DJH_STM32_HOST_SANITY=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    info "Building the ARM STM32 static library..."
    ensure_cmake_build "${STM32_ARM_BUILD}" -S link_libraries/stm32_lib \
        -DCMAKE_TOOLCHAIN_FILE="${STM32_TOOLCHAIN}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
}

# check_native_link_inputs DIR...: native builds link no Arduino carrier.
check_native_link_inputs() {
    if find "$@" -type f -name core.a -print -quit | grep -q .; then
        fail "Native build produced an unexpected core.a"
        exit 1
    fi
    if grep -R -E '(^|[ /])core\.a([[:space:]]|$)|arduino-pico|Arduino\.h' "$@" \
            --include='link.txt' --include='*.map' --include='compile_commands.json' \
            --include='CMakeCache.txt'; then
        fail "Native build metadata references the removed carrier"
        exit 1
    fi
}

# run_esp_build LOG OUTPUT COMMAND...: an ESP-IDF build whose failure also
# shows the end of its build.log and the runner's failure diagnostic.
run_esp_build() {
    local log="$1" output="$2"
    shift 2
    if "$@" 2>&1 | tee "${log}"; then
        return 0
    fi
    fail "Command failed: $*"
    if [[ -f "${output}/build.log" ]]; then
        echo "Last 300 lines of the ESP-IDF build log:"
        tail -n 300 "${output}/build.log"
    fi
    echo "ESP-IDF failure diagnostic:"
    if [[ -f "${output}/jh_esp_idf_failure.txt" ]]; then
        sed -n '1,120p' "${output}/jh_esp_idf_failure.txt"
    else
        echo "Missing failure diagnostic: ${output}/jh_esp_idf_failure.txt"
    fi
    exit 1
}

# ── Stage bodies ─────────────────────────────────────────────────────────────
stage_tools() {
    "${SCRIPT_DIR}/scripts/install_host_tools.sh" --check
    info "Verifying pinned third-party components..."
    "${SCRIPT_DIR}/third_party/update_components.sh" --verify-only
}

stage_repository() {
    info "Checking tracked generated artifacts..."
    if ! python3 scripts/sync_generated.py --check --report-file "${GENERATED_REPORT}"; then
        fail "Generated artifacts are stale; refresh them with python3 scripts/sync_generated.py --write"
        exit 1
    fi
    info "Checking release metadata..."
    if [[ -n "${JH_RELEASE_TAG:-}" ]]; then
        python3 scripts/check_release_metadata.py \
            --tag "${JH_RELEASE_TAG}" --release-ref origin/main
    else
        python3 scripts/check_release_metadata.py
    fi
    info "Linting the effective feature configuration of every project..."
    python3 scripts/generate_hal_features.py \
        --lint --effective --input-root . \
        --resolution-output "${GATE_BUILD_ROOT}/effective-feature-resolution.json"
}

stage_host() {
    rm -rf "${HOST_BUILD}"
    ensure_host_build
    info "Running tests..."
    ctest --test-dir "${HOST_BUILD}" --output-on-failure
}

stage_sanitizer_fuzz() {
    run_logged "${LOG_ROOT}/jh_sanitizer_fuzz.log" \
        "${SCRIPT_DIR}/scripts/run_sanitizer_fuzz.sh" \
            --build-dir "${GATE_BUILD_ROOT}/sanitizer-fuzz" \
            --jobs "${JOBS}"
}

MEMCHECK_REQUIRED_TESTS=(
    test_lwip_raw_engines
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

stage_memcheck() {
    local memcheck_tests memcheck_count test_name defects
    ensure_host_build

    info "Verifying native C/C++ memcheck coverage in CTest..."
    memcheck_tests=$(ctest --test-dir "${HOST_BUILD}" -N \
        -L '^memcheck$' 2>/dev/null || true)
    for test_name in "${MEMCHECK_REQUIRED_TESTS[@]}"; do
        if ! grep -qE ":[[:space:]]+${test_name}$" <<<"${memcheck_tests}"; then
            fail "Required test is missing from the native memcheck set: ${test_name}"
            exit 1
        fi
    done
    memcheck_count=$(grep -cE '^[[:space:]]*Test[[:space:]]+#[0-9]+:' \
        <<<"${memcheck_tests}" || true)
    if [[ "${memcheck_count}" -eq 0 ]]; then
        fail "CTest did not expose any native tests labelled memcheck"
        exit 1
    fi

    info "Running all ${memcheck_count} native C/C++ tests under Valgrind..."
    if ! ctest --test-dir "${HOST_BUILD}" -T memcheck \
        -L '^memcheck$' --output-on-failure 2>&1 \
        | tee "${LOG_ROOT}/jh_memcheck.log"; then
        fail "Valgrind memcheck execution failed; see ${LOG_ROOT}/jh_memcheck.log"
        exit 1
    fi

    if grep -q "Memory checking results:" "${LOG_ROOT}/jh_memcheck.log"; then
        defects=$(grep "Memory checking results:" "${LOG_ROOT}/jh_memcheck.log" | grep -oP '\d+ defect' | head -1 || true)
        if [[ -n "$defects" && "$defects" != "0 defect" ]]; then
            fail "Valgrind found memory defects!"
            grep -A5 "Memory checking results:" "${LOG_ROOT}/jh_memcheck.log"
            exit 1
        fi
    fi
}

stage_cppcheck() {
    info "Scanning src/ (vendored code excluded) with the pinned cppcheck..."
    "${SCRIPT_DIR}/scripts/run_cppcheck.sh"
}

# run_tidy_pass LABEL BUILD PROFILE LOG: clang-tidy over the files PROFILE
# selects from BUILD's compile database, output kept in LOG.
run_tidy_pass() {
    local label="$1" build="$2" profile="$3" log="$4"
    local tidy_db="${build}/clang_tidy_db"
    local files=()
    info "Running clang-tidy on ${label}..."
    mapfile -t files < <(
        scripts/clang_tidy_files.py --build-dir "${build}" --repo-root "${SCRIPT_DIR}" --profile "${profile}" \
            --output-compile-db "${tidy_db}/compile_commands.json"
    )
    if [[ "${#files[@]}" -eq 0 ]]; then
        fail "clang-tidy ${label} file list is empty"
        exit 1
    fi
    run-clang-tidy -p "${tidy_db}" -quiet "${files[@]}" 2>&1 | tee "${log}"
}

stage_clang_tidy() {
    local plain_host="${GATE_BUILD_ROOT}/host-plain"
    ensure_host_build
    ensure_stm32_builds
    # Without the FreeRTOS POSIX tests a source can come from another target
    # (jh_network_service.cpp: mock instead of a FreeRTOS LoRa test), so both
    # host databases are checked. Configuring is enough for that one.
    rm -rf "${plain_host}"
    cmake -S . -B "${plain_host}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    # The STM32 backend is checked in both builds: the ARM one sees the
    # hardware branches, the host-compiler one a 64-bit size_t.
    local tidy_logs=(
        "${LOG_ROOT}/jh_tidy_host.log"
        "${LOG_ROOT}/jh_tidy_host_plain.log"
        "${LOG_ROOT}/jh_tidy_stm32.log"
        "${LOG_ROOT}/jh_tidy_stm32_host.log"
    )
    run_tidy_pass "host-compilable code" "${HOST_BUILD}" host "${tidy_logs[0]}"
    run_tidy_pass "host-compilable code (no FreeRTOS POSIX)" "${plain_host}" host "${tidy_logs[1]}"
    run_tidy_pass "STM32 backend (ARM)" "${STM32_ARM_BUILD}" stm32 "${tidy_logs[2]}"
    run_tidy_pass "STM32 backend (host compiler)" "${STM32_HOST_BUILD}" stm32 "${tidy_logs[3]}"

    if grep -qE ':[0-9]+:[0-9]+: (warning|error):' "${tidy_logs[@]}" 2>/dev/null; then
        fail "clang-tidy reported findings:"
        grep -E ':[0-9]+:[0-9]+: (warning|error):' "${tidy_logs[@]}" \
            2>/dev/null | head -20
        exit 1
    fi
}

stage_cpd() {
    info "Scanning owned C/C++ implementations and Python scripts for duplication..."
    run_logged "${LOG_ROOT}/jh_cpd.log" \
        scripts/run_cpd.py --output-dir "${GATE_BUILD_ROOT}/cpd"
}

stage_stm32() {
    local build sx127x_build="${GATE_BUILD_ROOT}/stm32-target-sx127x"
    ensure_stm32_builds
    for build in "${STM32_HOST_BUILD}" "${STM32_ARM_BUILD}"; do
        if [[ ! -f "${build}/libJaszczurHAL.a" ]]; then
            fail "libJaszczurHAL.a not found in ${build}"
            exit 1
        fi
    done

    info "Building the ARM STM32 SX1276/SX1278 static library..."
    rm -rf "${sx127x_build}"
    cmake -S link_libraries/stm32_lib -B "${sx127x_build}" \
        -DCMAKE_TOOLCHAIN_FILE="${STM32_TOOLCHAIN}" \
        -DEXTRA_HAL_DEFINES=HAL_ENABLE_SX127X
    cmake --build "${sx127x_build}" --parallel "${JOBS}"
}

RP_PICO_TARGETS=(
    rp2040
    rp2350-arm
    rp2350-riscv
)

# Hand-picked RP2040 feature sets on bare metal. library-rp2040 builds every
# feature, but --all-features also turns FreeRTOS on; all-enabled keeps the
# wide set compiling without it.
RP_FLAG_PROFILES=(
    empty-core
    lora-sx127x
    typical-set
    udp-wireguard
    pim730-owned
    sdlogger
    all-enabled
)

rp_profile_flags() {
    case "$1" in
        empty-core)
            ;;
        lora-sx127x)
            echo -D HAL_ENABLE_SX127X
            ;;
        typical-set)
            echo -D HAL_ENABLE_WIFI -D HAL_ENABLE_MQTT -D HAL_ENABLE_KV \
                -D HAL_ENABLE_PCF8563 -D HAL_ENABLE_MCP9600 -D HAL_ENABLE_DS18B20 \
                -D HAL_ENABLE_GPS -D HAL_ENABLE_ILI9341 -D HAL_DISPLAY_ILI9341 \
                -D HAL_ENABLE_PWM_FREQ -D HAL_NETWORK_BACKEND_CYW43 \
                -D HAL_CYW43_BUS_PICO_PIO -D HAL_CYW43_STACK_LWIP \
                -D HAL_BOARD_PROFILE_RP_PICO_W \
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            ;;
        udp-wireguard)
            # UDP without TCP catches shared network helpers guarded by
            # HAL_ENABLE_TCP by mistake, and compiles the bundled
            # WireGuard/lwIP headers with the JaszczurHAL CYW43 stack.
            echo -D HAL_ENABLE_UDP -D HAL_ENABLE_WIREGUARD \
                -D HAL_NETWORK_BACKEND_CYW43 -D HAL_CYW43_BUS_PICO_PIO \
                -D HAL_CYW43_STACK_LWIP -D HAL_BOARD_PROFILE_RP_PICO_W \
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            ;;
        pim730-owned)
            echo -D HAL_ENABLE_WIFI -D HAL_ENABLE_TCP -D HAL_ENABLE_UDP \
                -D HAL_NETWORK_BACKEND_CYW43 -D HAL_CYW43_BUS_PICO_PIO \
                -D HAL_CYW43_STACK_LWIP -D HAL_BOARD_PROFILE_RP_PICO_PIM730 \
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            ;;
        sdlogger)
            echo -D HAL_ENABLE_SDLOGGER
            ;;
        all-enabled)
            echo -D HAL_ENABLE_WIFI -D HAL_ENABLE_TIME -D HAL_ENABLE_MQTT \
                -D HAL_ENABLE_UDP -D HAL_ENABLE_TCP -D HAL_ENABLE_BSD_SOCKETS \
                -D HAL_ENABLE_TLS -D HAL_ENABLE_HTTP_CLIENT \
                -D HAL_ENABLE_HTTP_SERVER -D HAL_ENABLE_HTTP_FILES \
                -D HAL_ENABLE_WEBSOCKET -D HAL_ENABLE_NET_CONSOLE \
                -D HAL_ENABLE_NET_COMMANDS -D HAL_ENABLE_OTA \
                -D HAL_ENABLE_WIREGUARD -D HAL_ENABLE_EEPROM -D HAL_ENABLE_KV \
                -D HAL_ENABLE_LITTLEFS -D HAL_ENABLE_FAT -D HAL_ENABLE_SDLOGGER \
                -D HAL_ENABLE_UART -D HAL_ENABLE_SWSERIAL -D HAL_ENABLE_I2C \
                -D HAL_ENABLE_I2C_SLAVE -D HAL_ENABLE_MCP2515 \
                -D HAL_ENABLE_PCF8563 -D HAL_ENABLE_DS3231 -D HAL_ENABLE_MCP9600 \
                -D HAL_ENABLE_MAX6675 -D HAL_ENABLE_DS18B20 -D HAL_ENABLE_ONEWIRE \
                -D HAL_ENABLE_EXTERNAL_ADC -D HAL_ENABLE_GPS -D HAL_ENABLE_DAC \
                -D HAL_ENABLE_PCNT -D HAL_ENABLE_PWM_FREQ -D HAL_ENABLE_RGB_LED \
                -D HAL_ENABLE_ILI9341 -D HAL_DISPLAY_ILI9341 \
                -D HAL_ENABLE_SSD1306 -D HAL_ENABLE_CRYPTO -D HAL_ENABLE_CJSON \
                -D HAL_NETWORK_BACKEND_CYW43 -D HAL_CYW43_BUS_PICO_PIO \
                -D HAL_CYW43_STACK_LWIP -D HAL_BOARD_PROFILE_RP_PICO_W \
                -D HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
            ;;
    esac
}

rp_profile_board() {
    case "$1" in
        typical-set|udp-wireguard|all-enabled) echo picow ;;
        pim730-owned) echo pico-rm2 ;;
        *) echo pico ;;
    esac
}

stage_rp() {
    local target profile build_dir flags
    for target in "${RP_PICO_TARGETS[@]}"; do
        info "Building Pico SDK target: ${target}"
        run_logged "${LOG_ROOT}/jh_rp_pico_${target}.log" \
            "${SCRIPT_DIR}/scripts/build_rp_pico_lib.sh" \
                --target "${target}" --example 01_core_runtime \
                --clean --jobs "${JOBS}" \
                --output "${GATE_BUILD_ROOT}/rp-pico/${target}/bare"
    done

    for target in "${RP_PICO_TARGETS[@]}"; do
        info "Building Pico SDK FreeRTOS SMP target: ${target}"
        run_logged "${LOG_ROOT}/jh_rp_pico_freertos_${target}.log" \
            "${SCRIPT_DIR}/scripts/build_rp_pico_lib.sh" \
                --target "${target}" --example 18_freertos_suite \
                --freertos --clean --jobs "${JOBS}" \
                --output "${GATE_BUILD_ROOT}/rp-pico/${target}/freertos"
    done

    for profile in "${RP_FLAG_PROFILES[@]}"; do
        build_dir="${GATE_BUILD_ROOT}/rp-pico-flags/${profile}"
        read -r -a flags <<<"$(rp_profile_flags "${profile}")"
        info "Building RP2040 flag profile: ${profile}"
        run_logged "${LOG_ROOT}/jh_rp_pico_${profile}.log" \
            "${SCRIPT_DIR}/scripts/build_rp_pico_lib.sh" \
                --target rp2040 --board "$(rp_profile_board "${profile}")" \
                --clean --jobs "${JOBS}" \
                --output "${build_dir}" "${flags[@]}"
        if [[ ! -f "${build_dir}/libJaszczurHAL.a" ]]; then
            fail "RP2040 flag profile ${profile} did not produce libJaszczurHAL.a"
            exit 1
        fi
    done

    info "Verifying native RP link inputs..."
    check_native_link_inputs "${GATE_BUILD_ROOT}/rp-pico" "${GATE_BUILD_ROOT}/rp-pico-flags"
}

stage_esp_idf() {
    local phase3="${GATE_BUILD_ROOT}/esp-idf/esp32s3-phase3"
    local gamepad="${GATE_BUILD_ROOT}/esp-idf/esp32-gamepad"
    local library="${GATE_BUILD_ROOT}/link-libraries/esp32s3"

    info "Building the ESP32-S3 Phase 3 fixture with pinned ESP-IDF..."
    run_esp_build "${LOG_ROOT}/jh_esp32s3_phase3.log" "${phase3}" \
        "${SCRIPT_DIR}/scripts/build_esp_idf.py" build \
            --project "${SCRIPT_DIR}/tests/fixtures/esp32s3_phase3" \
            --target esp32s3 \
            --board waveshare-esp32-s3-zero \
            --output "${phase3}" \
            --clean

    info "Building the ESP32 Classic gamepad fixture with pinned ESP-IDF..."
    run_esp_build "${LOG_ROOT}/jh_esp32_gamepad.log" "${gamepad}" \
        "${SCRIPT_DIR}/scripts/build_esp_idf.py" build \
            --project "${SCRIPT_DIR}/tests/fixtures/esp32_gamepad" \
            --target esp32 \
            --board esp32-devkitc-v4 \
            --output "${gamepad}" \
            --clean

    info "Checking the ESP-IDF clock configuration against the clock registry..."
    python3 "${SCRIPT_DIR}/scripts/clock_registry.py" \
        --esp-sdkconfig "esp32s3=${phase3}/sdkconfig" \
        --esp-sdkconfig "esp32=${gamepad}/sdkconfig"

    info "Building the ESP32-S3 all-features linkable library with pinned ESP-IDF..."
    run_esp_build "${LOG_ROOT}/jh_esp32s3_link_library.log" "${library}" \
        "${SCRIPT_DIR}/scripts/build_esp32_lib.sh" \
            --target esp32s3 \
            --all-features \
            --clean \
            --output "${library}"
    if [[ ! -f "${library}/libJaszczurHAL.a" ||
          ! -f "${library}/include/generated/jh_board_config.h" ]]; then
        fail "ESP32-S3 library runner did not publish libJaszczurHAL.a with its generated headers"
        exit 1
    fi
}

# build_library TARGET BOARD OUTPUT [RUNNER_ARGS...]: one all-features library.
build_library() {
    local target="$1" board="$2" output="$3"
    shift 3
    run_logged "${LOG_ROOT}/jh_library_${target}_$(basename "${output}").log" \
        "${SCRIPT_DIR}/scripts/build_link_library.sh" \
            --target "${target}" --board "${board}" \
            --all-features --library-only --clean --jobs "${JOBS}" \
            --output "${output}" "$@"
    if [[ ! -f "${output}/libJaszczurHAL.a" ]]; then
        fail "${target} all-features build did not produce libJaszczurHAL.a"
        exit 1
    fi
    check_native_link_inputs "${output}"
}

stage_library() {
    local target="$1" board="$2"
    local libraries="${GATE_BUILD_ROOT}/libraries/${target}"
    # An application header in a path with spaces, as under a Windows user
    # profile; the Windows CI job builds the same pairs through CMake.
    local consumer="${GATE_BUILD_ROOT}/library consumers/${target}"

    info "Building the ${target} library without a project header..."
    build_library "${target}" "${board}" "${libraries}/plain"

    info "Building the ${target} library for an application in a path with spaces..."
    rm -rf -- "${consumer}"
    python3 vscode/tools/create-vscode-example.py \
        --output "${consumer}" \
        --name "Library consumer ${target}" \
        --target "${target}" \
        --board "${board}"
    build_library "${target}" "${board}" "${libraries}/application" \
        --project-config "${consumer}"
}

stage_examples() {
    info "Building every $1 example configuration through jh-vscode..."
    run_logged "${LOG_ROOT}/jh_examples_$1_build.log" \
        "${SCRIPT_DIR}/scripts/examples_dispatcher.py" build \
            --target "$1" --jobs "${JOBS}"
}

stage_security() {
    # The scan covers ESP-IDF and its submodules, as in CI, where the test job
    # runs this stage after esp-idf.
    if [[ ! -e "${SCRIPT_DIR}/third_party/esp-idf/.git" ]]; then
        fail "The security scan needs ESP-IDF, as in CI: run the esp-idf stage first or scripts/ensure_esp_idf.sh --enable"
        exit 1
    fi
    JH_SECURITY_SCAN_SOURCE=1 "${SCRIPT_DIR}/scripts/check_vulnerabilities.sh"
}

run_stage() {
    local entry
    case "$1" in
        library-*)
            for entry in "${LIBRARY_BOARDS[@]}"; do
                if [[ "library-${entry%%:*}" == "$1" ]]; then
                    stage_library "${entry%%:*}" "${entry#*:}"
                fi
            done
            ;;
        examples-*) stage_examples "${1#examples-}" ;;
        *) "stage_${1//-/_}" ;;
    esac
}

# ── Main ─────────────────────────────────────────────────────────────────────
if [[ "${COMMIT_MODE}" -eq 1 ]]; then
    run_on_commit
fi

SECONDS=0
if [[ "${#SELECTED[@]}" -eq 0 ]]; then
    header "Clean start: removing build artifacts"
    clean_build_artifacts
    SELECTED=("${STAGES[@]}")
fi
mkdir -p "${LOG_ROOT}"

index=0
for stage in "${SELECTED[@]}"; do
    index=$((index + 1))
    header "Stage ${index}/${#SELECTED[@]}: ${stage} - $(stage_title "${stage}")"
    run_stage "${stage}"
    pass "Stage ${stage} passed."
done

# ═══════════════════════════════════════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════════════════════════════════════
echo ""
echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}${BOLD}  ALL STAGES PASSED ✓${NC}"
echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo ""
for stage in "${SELECTED[@]}"; do
    printf '  %-22s PASS\n' "${stage}"
done
if [[ -f "${GENERATED_REPORT}" ]]; then
    echo ""
    echo "  Generated artifacts:"
    while IFS= read -r report_line; do
        echo "    ${report_line}"
    done < "${GENERATED_REPORT}"
fi
echo ""
echo "  Total time: ${SECONDS}s"
echo ""
