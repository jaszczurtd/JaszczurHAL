# ─────────────────────────────────────────────────────────────────────────────
# CMake toolchain file for cross-compiling JaszczurHAL to STM32G474.
#
# Usage:
#   cmake -S stm32_lib -B build_stm32 \
#         -DCMAKE_TOOLCHAIN_FILE=stm32_lib/toolchain_stm32g474.cmake
#
# Optional cache variables:
#   ARM_GCC_PREFIX   (default: arm-none-eabi)
#   STM32_CPU        (default: cortex-m4)
#   STM32_FPU        (default: fpv4-sp-d16)
#   STM32_FLOAT_ABI  (default: hard)
# ─────────────────────────────────────────────────────────────────────────────

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(ARM_GCC_PREFIX "arm-none-eabi" CACHE STRING "GNU Arm Embedded toolchain prefix")

if(NOT CMAKE_C_COMPILER)
    find_program(CMAKE_C_COMPILER NAMES ${ARM_GCC_PREFIX}-gcc REQUIRED)
endif()
if(NOT CMAKE_CXX_COMPILER)
    find_program(CMAKE_CXX_COMPILER NAMES ${ARM_GCC_PREFIX}-g++ REQUIRED)
endif()
if(NOT CMAKE_AR)
    find_program(CMAKE_AR NAMES ${ARM_GCC_PREFIX}-ar REQUIRED)
endif()
if(NOT CMAKE_RANLIB)
    find_program(CMAKE_RANLIB NAMES ${ARM_GCC_PREFIX}-ranlib REQUIRED)
endif()
if(NOT CMAKE_OBJCOPY)
    find_program(CMAKE_OBJCOPY NAMES ${ARM_GCC_PREFIX}-objcopy)
endif()

set(STM32_CPU "cortex-m4" CACHE STRING "Target CPU")
set(STM32_FPU "fpv4-sp-d16" CACHE STRING "Target FPU")
set(STM32_FLOAT_ABI "hard" CACHE STRING "Target float ABI")

set(ARCH_FLAGS "-mcpu=${STM32_CPU} -mthumb -mfpu=${STM32_FPU} -mfloat-abi=${STM32_FLOAT_ABI}")
set(CMAKE_C_FLAGS_INIT "${ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${ARCH_FLAGS}")

# JaszczurHAL is built as a static library, so we avoid test-linking here.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
