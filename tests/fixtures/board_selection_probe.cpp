#include "hal/system/hal_board.h"

#ifndef JH_EXPECT_PROFILE
#error "JH_EXPECT_PROFILE is required."
#endif
#ifndef JH_EXPECT_USB
#define JH_EXPECT_USB 0
#endif
#ifndef JH_EXPECT_CYW43
#define JH_EXPECT_CYW43 0
#endif
#ifndef JH_EXPECT_EXTERNAL_RADIO
#define JH_EXPECT_EXTERNAL_RADIO 0
#endif
#ifndef JH_EXPECT_BLUETOOTH
#define JH_EXPECT_BLUETOOTH 0
#endif
#ifndef JH_EXPECT_SX1262
#define JH_EXPECT_SX1262 0
#endif
#ifndef JH_EXPECT_LED
#define JH_EXPECT_LED 0
#endif
#ifndef JH_EXPECT_NAME
#error "JH_EXPECT_NAME is required."
#endif

static_assert(HAL_BOARD_PROFILE_ID == JH_EXPECT_PROFILE);
static_assert(HAL_BOARD_HAS_USB_DEVICE == JH_EXPECT_USB);
static_assert(HAL_BOARD_HAS_CYW43 == JH_EXPECT_CYW43);
static_assert(HAL_BOARD_HAS_EXTERNAL_RADIO_FRONTEND ==
              JH_EXPECT_EXTERNAL_RADIO);
static_assert(HAL_BOARD_HAS_BLUETOOTH_CONTROLLER == JH_EXPECT_BLUETOOTH);
static_assert(HAL_BOARD_HAS_SX1262_RADIO == JH_EXPECT_SX1262);

#if JH_EXPECT_LED
static_assert(HAL_LED_BUILTIN == JH_EXPECT_LED);
#endif

constexpr bool board_name_is(const char *lhs, const char *rhs) {
  while (*lhs != '\0' && *rhs != '\0') {
    if (*lhs++ != *rhs++) {
      return false;
    }
  }
  return *lhs == *rhs;
}

static_assert(board_name_is(HAL_BOARD_PROFILE_NAME, JH_EXPECT_NAME));

#if defined(JH_EXPECT_NO_LED_BUILTIN) && defined(HAL_LED_BUILTIN)
#error "HAL_LED_BUILTIN must not be defined for this profile."
#endif

#if defined(JH_EXPECT_LEGACY_PICOW)
#ifndef HAL_CYW43_PROFILE_PICOW
#error "Pico W compatibility profile was not derived."
#endif
#endif

#if defined(JH_EXPECT_LEGACY_PIM730)
#ifndef HAL_CYW43_PROFILE_PIM730
#error "PIM730 compatibility profile was not derived."
#endif
#endif

int main(void) { return 0; }
