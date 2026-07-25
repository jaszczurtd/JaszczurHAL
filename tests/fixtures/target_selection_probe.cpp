#include "hal/hal_target.h"

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
static_assert(HAL_TARGET_IS_RP == JH_EXPECT_RP);
static_assert(HAL_RP_ARCH_ARM == JH_EXPECT_ARM);
static_assert(HAL_RP_ARCH_RISCV == JH_EXPECT_RISCV);
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
#elif defined(JH_EXPECT_NAME_MOCK)
static_assert(target_name_is(HAL_TARGET_NAME, "mock"));
#else
#error "Target selection probe requires an expected target name."
#endif

int main(void) { return 0; }
