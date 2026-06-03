#!/usr/bin/env bash
# Build the portable_blink demo for STM32G474 (Nucleo-G474RE) into a flashable
# ELF/BIN/HEX. Shares blink_app.c with the RP2040 sketch one level up.
#
# Requires the GNU Arm Embedded toolchain (arm-none-eabi-gcc/g++) on PATH.
#   ./build.sh                 # normal blink
#   FAULT_TEST=1 ./build.sh    # (reserved) fault self-test variant
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
JH_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
SRC="${JH_ROOT}/src"
G474="${SRC}/hal/impl/stm32g474"
LD="${JH_ROOT}/stm32_lib/STM32G474RETx_FLASH.ld"
OUT="${SCRIPT_DIR}/build"
mkdir -p "${OUT}"

ARCH="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
# Target selected through the canonical switch; ARM arch derives JH_STM32G474_HW.
DEFS="-DHAL_TARGET_STM32G474=1"
CFLAGS="${ARCH} -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra -O2 ${DEFS} -include ${SRC}/hal/hal_target.h -I${EXAMPLE_DIR} -I${SRC} -I${SRC}/hal -I${G474}"

C_SOURCES=(
    "${SCRIPT_DIR}/main.c"
    "${EXAMPLE_DIR}/blink_app.c"
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

echo "  LD  portable_blink_g474.elf"
arm-none-eabi-g++ ${ARCH} -T"${LD}" -nostartfiles -Wl,--gc-sections \
    -Wl,-Map="${OUT}/portable_blink_g474.map" --specs=nano.specs --specs=nosys.specs \
    "${OBJS[@]}" -o "${OUT}/portable_blink_g474.elf"

arm-none-eabi-objcopy -O binary "${OUT}/portable_blink_g474.elf" "${OUT}/portable_blink_g474.bin"
arm-none-eabi-objcopy -O ihex   "${OUT}/portable_blink_g474.elf" "${OUT}/portable_blink_g474.hex"
arm-none-eabi-size "${OUT}/portable_blink_g474.elf"
echo "Artifacts in ${OUT}/ (elf/bin/hex)"
