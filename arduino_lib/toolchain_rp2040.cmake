# ─────────────────────────────────────────────────────────────────────────────
# CMake toolchain file for cross-compiling JaszczurHAL to RP2040 (Arduino)
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=../arduino_lib/toolchain_rp2040.cmake \
#         -DARDUINO_ROOT=~/.arduino15/packages/rp2040 \
#         ../arduino_lib
#
# The only required variable is ARDUINO_ROOT (base of the rp2040 package).
# All other paths are auto-detected from the installed Arduino core version
# and toolchain version.
# ─────────────────────────────────────────────────────────────────────────────

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ── Locate Arduino root ─────────────────────────────────────────────────────
if(NOT DEFINED ARDUINO_ROOT)
    set(ARDUINO_ROOT "$ENV{HOME}/.arduino15/packages/rp2040")
endif()

# ── Auto-detect toolchain version ───────────────────────────────────────────
if(NOT DEFINED ARDUINO_TOOLCHAIN_VERSION)
    file(GLOB _tc_versions "${ARDUINO_ROOT}/tools/pqt-gcc/*")
    list(SORT _tc_versions ORDER DESCENDING)
    list(GET _tc_versions 0 _tc_path)
    get_filename_component(ARDUINO_TOOLCHAIN_VERSION "${_tc_path}" NAME)
endif()

set(TOOLCHAIN_BIN "${ARDUINO_ROOT}/tools/pqt-gcc/${ARDUINO_TOOLCHAIN_VERSION}/bin")

# ── Auto-detect core version ────────────────────────────────────────────────
if(NOT DEFINED ARDUINO_CORE_VERSION)
    file(GLOB _core_versions "${ARDUINO_ROOT}/hardware/rp2040/*")
    list(SORT _core_versions ORDER DESCENDING)
    list(GET _core_versions 0 _core_path)
    get_filename_component(ARDUINO_CORE_VERSION "${_core_path}" NAME)
endif()

set(ARDUINO_PLATFORM_DIR "${ARDUINO_ROOT}/hardware/rp2040/${ARDUINO_CORE_VERSION}")

# ── Board / variant configuration ───────────────────────────────────────────
set(ARDUINO_CHIP "rp2040" CACHE STRING "Target chip (rp2040)")
set(ARDUINO_VARIANT "rpipico" CACHE STRING "Board variant directory name")

set(ARDUINO_CORE_PATH "${ARDUINO_PLATFORM_DIR}/cores/rp2040" CACHE PATH
    "Path to Arduino core headers")
set(ARDUINO_VARIANT_PATH "${ARDUINO_PLATFORM_DIR}/variants/${ARDUINO_VARIANT}" CACHE PATH
    "Path to board variant headers")

# ── Compilers ────────────────────────────────────────────────────────────────
set(CMAKE_C_COMPILER   "${TOOLCHAIN_BIN}/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/arm-none-eabi-g++")
set(CMAKE_AR           "${TOOLCHAIN_BIN}/arm-none-eabi-ar")
set(CMAKE_RANLIB       "${TOOLCHAIN_BIN}/arm-none-eabi-ranlib")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_BIN}/arm-none-eabi-objcopy")

# ── Architecture flags ──────────────────────────────────────────────────────
set(ARCH_FLAGS "-march=armv6-m -mcpu=cortex-m0plus -mthumb")
set(CMAKE_C_FLAGS_INIT   "${ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS}")

# ── Prevent CMake from test-linking (no sysroot / startup code) ─────────────
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
