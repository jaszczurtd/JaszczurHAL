#!/usr/bin/env bash
# Build the JaszczurHAL STM32G474 blink + exception-info demo into a flashable
# ELF/BIN/HEX for the Nucleo-G474RE.
#
# Requires the GNU Arm Embedded toolchain (arm-none-eabi-gcc/g++) on PATH.
#
# Usage:
#   ./build.sh                 # normal blink
#   FAULT_TEST=1 ./build.sh    # build the fault self-test variant
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JH_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SRC="${JH_ROOT}/src"
G474="${SRC}/hal/impl/stm32g474"
LD="${JH_ROOT}/stm32_lib/STM32G474RETx_FLASH.ld"
OUT="${SCRIPT_DIR}/build"
mkdir -p "${OUT}"

ARCH="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
# Target is selected through the canonical project-config switch. The ARM arch
# makes hal_target.h derive JH_STM32G474_HW (real registers vs host stub).
DEFS="-DHAL_TARGET_STM32G474=1"
[ "${FAULT_TEST:-0}" = "1" ] && DEFS="${DEFS} -DJH_BLINK_FAULT_TEST=1"
CFLAGS="${ARCH} -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra -O2 ${DEFS} -include ${SRC}/hal/hal_target.h -I${SRC}/hal -I${G474}"

C_SOURCES=(
    "${SCRIPT_DIR}/main.c"
    "${G474}/port/startup_stm32g474.c"
    "${G474}/port/system_stm32g474.c"
    "${G474}/port/g474_debug_uart.c"
    "${G474}/port/exception_info.c"
)
CXX_SOURCES=(
    "${G474}/hal_gpio.cpp"
    "${G474}/hal_serial.cpp"
    "${G474}/hal_system.cpp"
    "${G474}/hal_sync.cpp"
    "${SRC}/hal/hal_config.cpp"
    "${G474}/drivers/stm32g474/stm32g474_system.cpp"
    "${G474}/drivers/stm32g474/stm32g474_fault.cpp"
)

OBJS=()
for f in "${C_SOURCES[@]}"; do
    o="${OUT}/$(basename "${f%.c}").o"
    echo "  CC  $(basename "$f")"
    arm-none-eabi-gcc ${CFLAGS} -c "$f" -o "$o"
    OBJS+=("$o")
done
for f in "${CXX_SOURCES[@]}"; do
    o="${OUT}/$(basename "${f%.cpp}").o"
    echo "  CXX $(basename "$f")"
    arm-none-eabi-g++ ${CFLAGS} -fno-exceptions -fno-rtti -c "$f" -o "$o"
    OBJS+=("$o")
done

echo "  LD  blink_g474.elf"
arm-none-eabi-g++ ${ARCH} -T"${LD}" -nostartfiles -Wl,--gc-sections \
    -Wl,-Map="${OUT}/blink_g474.map" --specs=nano.specs --specs=nosys.specs \
    "${OBJS[@]}" -o "${OUT}/blink_g474.elf"

arm-none-eabi-objcopy -O binary "${OUT}/blink_g474.elf" "${OUT}/blink_g474.bin"
arm-none-eabi-objcopy -O ihex   "${OUT}/blink_g474.elf" "${OUT}/blink_g474.hex"
arm-none-eabi-size "${OUT}/blink_g474.elf"
echo "Artifacts in ${OUT}/ (elf/bin/hex)"
