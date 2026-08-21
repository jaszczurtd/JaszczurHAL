#if defined(JH_PROBE_UMBRELLA)
#include "JaszczurHAL.h"
#elif defined(JH_PROBE_HAL_CONFIG)
#include "hal/core/hal_config.h"
#elif defined(JH_PROBE_FORCED_TARGET)
#include "hal/system/hal_board.h"
#else
#include "hal/core/hal_target.h"
#if defined(JH_EXPECT_BOARD_ID)
#include "hal/system/hal_board.h"
#endif
#endif

#ifndef JH_EXPECT_RP2040
#define JH_EXPECT_RP2040 0
#endif
#ifndef JH_EXPECT_RP2350_ARM
#define JH_EXPECT_RP2350_ARM 0
#endif
#ifndef JH_EXPECT_RP2350_RISCV
#define JH_EXPECT_RP2350_RISCV 0
#endif
#ifndef JH_EXPECT_STM32G474
#define JH_EXPECT_STM32G474 0
#endif
#ifndef JH_EXPECT_MOCK
#define JH_EXPECT_MOCK 0
#endif
#ifndef JH_EXPECT_ESP32_S3
#define JH_EXPECT_ESP32_S3 0
#endif
#ifndef JH_EXPECT_ESP32_FAMILY
#define JH_EXPECT_ESP32_FAMILY 0
#endif
#ifndef JH_EXPECT_RP
#define JH_EXPECT_RP 0
#endif
#ifndef JH_EXPECT_ARM
#define JH_EXPECT_ARM 0
#endif
#ifndef JH_EXPECT_RISCV
#define JH_EXPECT_RISCV 0
#endif
static_assert(HAL_TARGET_IS_RP2040 == JH_EXPECT_RP2040);
static_assert(HAL_TARGET_IS_RP2350_ARM == JH_EXPECT_RP2350_ARM);
static_assert(HAL_TARGET_IS_RP2350_RISCV == JH_EXPECT_RP2350_RISCV);
static_assert(HAL_TARGET_IS_STM32G474 == JH_EXPECT_STM32G474);
static_assert(HAL_TARGET_IS_MOCK == JH_EXPECT_MOCK);
static_assert(HAL_TARGET_IS_ESP32_S3 == JH_EXPECT_ESP32_S3);
static_assert(HAL_TARGET_IS_ESP32_FAMILY == JH_EXPECT_ESP32_FAMILY);
static_assert(HAL_TARGET_IS_RP == JH_EXPECT_RP);
static_assert(HAL_RP_ARCH_ARM == JH_EXPECT_ARM);
static_assert(HAL_RP_ARCH_RISCV == JH_EXPECT_RISCV);
#if defined(JH_EXPECT_BOARD_ID)
static_assert(HAL_BOARD_PROFILE_ID == JH_EXPECT_BOARD_ID);
#endif
#if defined(JH_EXPECT_EEPROM_TYPE)
static_assert(HAL_EEPROM_TYPE == JH_EXPECT_EEPROM_TYPE);
static_assert(HAL_AT24C256_PAGE_SIZE == 128u);
#endif
constexpr bool target_name_is(const char *lhs, const char *rhs) {
  while (*lhs != '\0' && *rhs != '\0') {
    if (*lhs++ != *rhs++) {
      return false;
    }
  }
  return *lhs == *rhs;
}

#if defined(JH_EXPECT_NAME_RP2040)
static_assert(target_name_is(HAL_TARGET_NAME, "rp2040"));
#elif defined(JH_EXPECT_NAME_RP2350_ARM)
static_assert(target_name_is(HAL_TARGET_NAME, "rp2350-arm"));
#elif defined(JH_EXPECT_NAME_RP2350_RISCV)
static_assert(target_name_is(HAL_TARGET_NAME, "rp2350-riscv"));
#elif defined(JH_EXPECT_NAME_STM32G474)
static_assert(target_name_is(HAL_TARGET_NAME, "stm32g474"));
#elif defined(JH_EXPECT_NAME_ESP32S3)
static_assert(target_name_is(HAL_TARGET_NAME, "esp32s3"));
#elif defined(JH_EXPECT_NAME_MOCK)
static_assert(target_name_is(HAL_TARGET_NAME, "mock"));
#else
#error "Target selection probe requires an expected target name."
#endif

int main(void) { return 0; }
