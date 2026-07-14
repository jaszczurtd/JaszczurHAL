#include "../../hal_target.h"
#if HAL_TARGET_IS_STM32G474
#include "../../hal_config.h"
#ifdef HAL_ENABLE_RGB_LED

#include "../../hal_gpio.h"
#include "../../hal_rgb_led.h"
#include "../../hal_sync.h"
#include "../shared/drivers/neopixel/jh_neopixel.h"
#include "../shared/hal_mutex_once.h"

#ifdef JH_STM32G474_HW
#include "port/stm32g474_regs.h"
#endif

static jh_neopixel_t s_strip;
static bool s_strip_ready = false;
static int s_last_color = -1;
static uint8_t s_brightness = 30;
static hal_mutex_t s_rgb_mutex = nullptr;

#ifdef JH_STM32G474_HW
#define STM32_DEMCR (*(volatile uint32_t *)0xE000EDFCu)
#define STM32_DWT_CTRL (*(volatile uint32_t *)0xE0001000u)
#define STM32_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define STM32_DEMCR_TRCENA (1u << 24)
#define STM32_DWT_CYCCNTENA (1u << 0)

static inline void stm32_cycles_enable(void) {
  STM32_DEMCR |= STM32_DEMCR_TRCENA;
  STM32_DWT_CTRL |= STM32_DWT_CYCCNTENA;
}

static inline void stm32_wait_until(uint32_t target) {
  while ((int32_t)(STM32_DWT_CYCCNT - target) < 0) {
  }
}
#endif

static bool rgb_pixel_type_valid(hal_rgb_led_pixel_type_t pixel_type) {
  return pixel_type == HAL_RGB_LED_PIXEL_RGB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_GRB_KHZ800 ||
         pixel_type == HAL_RGB_LED_PIXEL_RGBW_KHZ800;
}

static hal_status_t rgb_lock(void) {
  if (jh_hal_mutex_create_once(&s_rgb_mutex) == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_rgb_mutex);
  return HAL_OK;
}

static void rgb_unlock(void) { hal_mutex_unlock(s_rgb_mutex); }

static bool stm32_write_pixels(const uint8_t *pixels, uint32_t num_bytes,
                               bool is800khz, uint8_t pin, void *user) {
  (void)user;
  if (!pixels || num_bytes == 0u) {
    return true;
  }

#ifdef JH_STM32G474_HW
  if (!is800khz) {
    return false;
  }

  const uint32_t port = (uint32_t)(pin >> 4);
  const uint32_t pin_num = (uint32_t)(pin & 0x0Fu);
  if (port > 6u) {
    return false;
  }

  volatile uint32_t *const bsrr = &GPIO_BSRR(port);
  const uint32_t set_mask = (1u << pin_num);
  const uint32_t clr_mask = (1u << (pin_num + 16u));

  stm32_cycles_enable();

  /* WS2812B timings @16 MHz (62.5 ns/cycle):
   * T0H=0.35 us -> 6 cycles, T1H=0.8 us -> 13 cycles, total=1.25 us -> 20.
   */
  const uint32_t bit_cycles = JH_G474_CORE_CLOCK_HZ / 800000u;
  const uint32_t t0h_cycles =
      ((JH_G474_CORE_CLOCK_HZ * 35u) + 50000000u) / 100000000u;
  const uint32_t t1h_cycles =
      ((JH_G474_CORE_CLOCK_HZ * 80u) + 50000000u) / 100000000u;

  if (bit_cycles == 0u || t0h_cycles == 0u || t1h_cycles == 0u) {
    return false;
  }
  if (!(t0h_cycles < t1h_cycles && t1h_cycles < bit_cycles)) {
    return false;
  }

  hal_critical_section_enter();

  uint32_t start = STM32_DWT_CYCCNT;
  for (uint32_t i = 0; i < num_bytes; ++i) {
    uint8_t value = pixels[i];
    for (uint8_t mask = 0x80u; mask != 0u; mask >>= 1u) {
      const uint32_t bit_start = start;
      *bsrr = set_mask;
      const uint32_t high_until =
          bit_start + ((value & mask) ? t1h_cycles : t0h_cycles);
      stm32_wait_until(high_until);
      *bsrr = clr_mask;
      const uint32_t bit_until = bit_start + bit_cycles;
      stm32_wait_until(bit_until);
      start = bit_until;
    }
  }

  hal_critical_section_exit();
  return true;
#else
  (void)is800khz;
  (void)pin;
  return true;
#endif
}

static hal_status_t s_init_with_type(uint8_t pin, uint8_t num_pixels,
                                     uint16_t neo_type) {
  if (s_strip_ready) {
    jh_neopixel_deinit(&s_strip);
    s_strip_ready = false;
  }

  if (!jh_neopixel_init(&s_strip, num_pixels, pin, neo_type)) {
    return HAL_ENOMEM;
  }

  hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT);
  hal_gpio_write(pin, false);
  s_strip_ready = true;
  s_last_color = -1;
  return HAL_OK;
}

hal_status_t hal_rgb_led_init(uint8_t pin, uint8_t num_pixels) {
  return hal_rgb_led_init_ex(pin, num_pixels, HAL_RGB_LED_PIXEL_RGB_KHZ800);
}

hal_status_t hal_rgb_led_init_ex(uint8_t pin, uint8_t num_pixels,
                                 hal_rgb_led_pixel_type_t pixel_type) {
  if ((pin >> 4) > 6u || num_pixels == 0u ||
      !rgb_pixel_type_valid(pixel_type)) {
    return HAL_EINVAL;
  }
  const hal_status_t lock_status = rgb_lock();
  if (hal_status_is_error(lock_status)) {
    return lock_status;
  }
  const hal_status_t status =
      s_init_with_type(pin, num_pixels, (uint16_t)pixel_type);
  rgb_unlock();
  return status;
}

void hal_rgb_led_set_brightness(uint8_t brightness) {
  if (hal_status_is_error(rgb_lock())) {
    return;
  }
  s_brightness = (brightness < 1u) ? 1u : brightness;
  s_last_color = -1;
  rgb_unlock();
}

hal_status_t hal_rgb_led_off(void) {
  return hal_rgb_led_set_color(HAL_RGB_LED_NONE);
}

hal_status_t hal_rgb_led_set_color(hal_rgb_led_color_t color) {
  if (color < HAL_RGB_LED_NONE || color > HAL_RGB_LED_PURPLE) {
    return HAL_EINVAL;
  }
  const hal_status_t lock_status = rgb_lock();
  if (hal_status_is_error(lock_status)) {
    return lock_status;
  }
  if (!s_strip_ready) {
    rgb_unlock();
    return HAL_EUNINIT;
  }
  if ((int)color == s_last_color) {
    rgb_unlock();
    return HAL_OK;
  }

  uint8_t r = 0u;
  uint8_t g = 0u;
  uint8_t b = 0u;
  const uint8_t br = s_brightness;

  switch (color) {
  case HAL_RGB_LED_RED:
    r = br;
    break;
  case HAL_RGB_LED_GREEN:
    g = br;
    break;
  case HAL_RGB_LED_BLUE:
    b = br;
    break;
  case HAL_RGB_LED_YELLOW:
    r = br;
    g = br;
    break;
  case HAL_RGB_LED_WHITE:
    r = br;
    g = br;
    b = br;
    break;
  case HAL_RGB_LED_PURPLE:
    r = br;
    b = br;
    break;
  default:
    break;
  }

  jh_neopixel_set_pixel_color_packed(&s_strip, 0u, jh_neopixel_color(r, g, b));
  const bool shown = jh_neopixel_show(&s_strip, stm32_write_pixels, nullptr);
  if (shown) {
    s_last_color = (int)color;
  }
  rgb_unlock();
  return shown ? HAL_OK : HAL_EIO;
}

#endif /* HAL_ENABLE_RGB_LED */
#endif /* HAL_TARGET_IS_STM32G474 */
