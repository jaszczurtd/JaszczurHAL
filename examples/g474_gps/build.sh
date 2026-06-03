#!/usr/bin/env bash
# Build the STM32G474 GPS engine demo (Nucleo-G474RE) into a flashable ELF/BIN/HEX.
# Requires the GNU Arm Embedded toolchain (arm-none-eabi-gcc/g++) on PATH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JH_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SRC="${JH_ROOT}/src"
G474="${SRC}/hal/impl/stm32g474"
SHARED="${SRC}/hal/impl/shared"
LD="${JH_ROOT}/stm32_lib/STM32G474RETx_FLASH.ld"
OUT="${SCRIPT_DIR}/build"
mkdir -p "${OUT}"

ARCH="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
DEFS="-DHAL_TARGET_STM32G474=1 -DHAL_ENABLE_GPS -DHAL_ENABLE_UART"
CFLAGS="${ARCH} -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra -O2 ${DEFS} -include ${SRC}/hal/hal_target.h -I${SRC} -I${SRC}/hal -I${G474}"

C_SOURCES=(
    "${SCRIPT_DIR}/main.c"
    "${G474}/port/startup_stm32g474.c"
    "${G474}/port/system_stm32g474.c"
    "${G474}/port/g474_debug_uart.c"
    "${G474}/port/exception_info.c"
)
CXX_SOURCES=(
    "${SHARED}/gps_nmea_parser.cpp"
    "${SHARED}/hal_gps_core.cpp"
    "${G474}/hal_gps.cpp"
    "${G474}/hal_uart.cpp"
    "${G474}/hal_serial.cpp"
    "${G474}/hal_system.cpp"
    "${G474}/hal_sync.cpp"
    "${SRC}/hal/hal_config.cpp"
    "${G474}/drivers/stm32g474/stm32g474_system.cpp"
    "${G474}/drivers/stm32g474/stm32g474_fault.cpp"
)

OBJS=()
for f in "${C_SOURCES[@]}"; do
    o="${OUT}/$(basename "${f%.c}").o"; echo "  CC  $(basename "$f")"
    arm-none-eabi-gcc ${CFLAGS} -c "$f" -o "$o"; OBJS+=("$o")
done
for f in "${CXX_SOURCES[@]}"; do
    o="${OUT}/$(basename "${f%.cpp}").o"; echo "  CXX $(basename "$f")"
    arm-none-eabi-g++ ${CFLAGS} -fno-exceptions -fno-rtti -c "$f" -o "$o"; OBJS+=("$o")
done

echo "  LD  g474_gps.elf"
arm-none-eabi-g++ ${ARCH} -T"${LD}" -nostartfiles -Wl,--gc-sections \
    --specs=nano.specs --specs=nosys.specs "${OBJS[@]}" -o "${OUT}/g474_gps.elf"
arm-none-eabi-objcopy -O binary "${OUT}/g474_gps.elf" "${OUT}/g474_gps.bin"
arm-none-eabi-objcopy -O ihex   "${OUT}/g474_gps.elf" "${OUT}/g474_gps.hex"
arm-none-eabi-size "${OUT}/g474_gps.elf"
echo "Artifacts in ${OUT}/ (elf/bin/hex)"
