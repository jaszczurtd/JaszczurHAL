cmake_minimum_required(VERSION 3.16)
#
# portable_blink for STM32G474 (Nucleo-G474RE). Shares blink_app.c with the
# RP2040 sketch one directory up.
#
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../../../stm32_lib/toolchain_stm32g474.cmake
#   cmake --build build
#
project(portable_blink_g474 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(EXAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(JH_ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(SRC      ${JH_ROOT}/src)
set(G474     ${SRC}/hal/impl/stm32g474)
set(LDSCRIPT ${JH_ROOT}/stm32_lib/STM32G474RETx_FLASH.ld)

add_executable(portable_blink_g474
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${EXAMPLE_DIR}/blink_app.c
    ${G474}/port/startup_stm32g474.c
    ${G474}/port/system_stm32g474.c
    ${G474}/port/g474_debug_uart.c
    ${G474}/port/exception_info.c
    ${G474}/hal_gpio.cpp
    ${G474}/hal_serial.cpp
    ${G474}/hal_system.cpp
    ${G474}/hal_sync.cpp
    ${SRC}/hal/hal_config.cpp
    ${G474}/drivers/stm32g474/stm32g474_system.cpp
    ${G474}/drivers/stm32g474/stm32g474_fault.cpp
)
set_target_properties(portable_blink_g474 PROPERTIES OUTPUT_NAME "portable_blink_g474" SUFFIX ".elf" LINKER_LANGUAGE CXX)

target_include_directories(portable_blink_g474 PRIVATE ${EXAMPLE_DIR} ${SRC} ${SRC}/hal ${G474})
target_compile_definitions(portable_blink_g474 PRIVATE HAL_TARGET_STM32G474=1)

set(ARCH_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
target_compile_options(portable_blink_g474 PRIVATE
    ${ARCH_FLAGS} -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra
    -include ${SRC}/hal/hal_target.h
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)
target_link_options(portable_blink_g474 PRIVATE
    ${ARCH_FLAGS} -T${LDSCRIPT} -nostartfiles -Wl,--gc-sections
    --specs=nano.specs --specs=nosys.specs
)
