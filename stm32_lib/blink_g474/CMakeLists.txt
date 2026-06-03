cmake_minimum_required(VERSION 3.16)
#
# Standalone flashable firmware: JaszczurHAL STM32G474 blink + exception-info.
#
# Configure with the ARM toolchain file and build:
#   cmake -S stm32_lib/blink_g474 -B build_blink_g474 \
#         -DCMAKE_TOOLCHAIN_FILE=../toolchain_stm32g474.cmake
#   cmake --build build_blink_g474
#
# Produces blink_g474.elf / .bin / .hex for the Nucleo-G474RE.
#
project(jh_blink_g474 C ASM)

set(CMAKE_C_STANDARD 11)

set(JH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SRC     ${JH_ROOT}/src)
set(G474    ${SRC}/hal/impl/stm32g474)
set(LDSCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../STM32G474RETx_FLASH.ld)

add_executable(blink_g474
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    # ── bring-up port layer ───────────────────────────────────────────────
    ${G474}/port/startup_stm32g474.c
    ${G474}/port/system_stm32g474.c
    ${G474}/port/g474_debug_uart.c
    ${G474}/port/exception_info.c
    # ── backend (real under JH_STM32G474_HW) ──────────────────────────────
    ${G474}/hal_gpio.cpp
    ${G474}/drivers/stm32g474/stm32g474_system.cpp
)

set_target_properties(blink_g474 PROPERTIES OUTPUT_NAME "blink_g474" SUFFIX ".elf")
enable_language(CXX)
set_target_properties(blink_g474 PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(blink_g474 PRIVATE
    ${SRC}/hal       # hal_gpio.h
    ${G474}          # drivers/..., port/...
)

# Select the backend through the canonical project-config switch. Building for
# ARM makes hal_target.h derive JH_STM32G474_HW (real registers vs host stub).
target_compile_definitions(blink_g474 PRIVATE HAL_TARGET_STM32G474=1)
# Force the target selector into every TU (incl. the port .c files).
target_compile_options(blink_g474 PRIVATE -include ${SRC}/hal/hal_target.h)

# Uncomment to build the fault self-test variant:
# target_compile_definitions(blink_g474 PRIVATE JH_BLINK_FAULT_TEST=1)

set(ARCH_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

target_compile_options(blink_g474 PRIVATE
    ${ARCH_FLAGS}
    -ffreestanding
    -ffunction-sections -fdata-sections
    -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)

target_link_options(blink_g474 PRIVATE
    ${ARCH_FLAGS}
    -T${LDSCRIPT}
    -nostartfiles
    -Wl,--gc-sections
    -Wl,-Map=blink_g474.map
    --specs=nano.specs
    --specs=nosys.specs
)

# Post-build: size report + raw bin/hex for flashing.
find_program(ARM_OBJCOPY NAMES ${ARM_GCC_PREFIX}-objcopy arm-none-eabi-objcopy)
find_program(ARM_SIZE    NAMES ${ARM_GCC_PREFIX}-size    arm-none-eabi-size)

if(ARM_OBJCOPY)
    add_custom_command(TARGET blink_g474 POST_BUILD
        COMMAND ${ARM_OBJCOPY} -O binary $<TARGET_FILE:blink_g474> blink_g474.bin
        COMMAND ${ARM_OBJCOPY} -O ihex   $<TARGET_FILE:blink_g474> blink_g474.hex)
endif()
if(ARM_SIZE)
    add_custom_command(TARGET blink_g474 POST_BUILD
        COMMAND ${ARM_SIZE} $<TARGET_FILE:blink_g474>)
endif()
