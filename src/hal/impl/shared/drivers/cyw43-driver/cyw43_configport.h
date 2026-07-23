#ifndef JASZCZURHAL_CYW43_CONFIGPORT_H
#define JASZCZURHAL_CYW43_CONFIGPORT_H

#include <stddef.h>
#include <stdint.h>

#ifndef __cplusplus
#define static_assert _Static_assert
#endif

#ifndef MIN
#define MIN(a, b) ((a) <= (b) ? (a) : (b))
#endif
#define CYW43_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define CYW43_USE_SPI (1)
#if defined(HAL_CYW43_STACK_LWIP)
#define CYW43_LWIP (1)
#else
#define CYW43_LWIP (0)
#endif
#define CYW43_NETUTILS (0)
#define CYW43_USE_STATS (0)
#define CYW43_USE_OTP_MAC (0)
#define CYW43_GPIO (1)
#define CYW43_ENABLE_BLUETOOTH (0)
#define CYW43_RESOURCE_VERIFY_DOWNLOAD (1)
#ifndef CYW43_WL_GPIO_COUNT
#define CYW43_WL_GPIO_COUNT (3)
#endif
#define CYW43_NUM_GPIOS CYW43_WL_GPIO_COUNT

/* Logical pin identities. The target transport owns the physical profile. */
#define CYW43_PIN_WL_REG_ON (0)
#define CYW43_PIN_WL_HOST_WAKE (1)

#define CYW43_EPERM (1)
#define CYW43_EIO (5)
#define CYW43_EINVAL (22)
#define CYW43_ETIMEDOUT (110)

#define CYW43_THREAD_ENTER                                                     \
  do {                                                                         \
  } while (0)
#define CYW43_THREAD_EXIT                                                      \
  do {                                                                         \
  } while (0)
#define CYW43_THREAD_LOCK_CHECK                                                \
  do {                                                                         \
  } while (0)
#define CYW43_SDPCM_SEND_COMMON_WAIT jh_cyw43_port_control_wait()
#define CYW43_DO_IOCTL_WAIT jh_cyw43_port_control_wait()
#define CYW43_EVENT_POLL_HOOK jh_cyw43_port_event_poll()

#define CYW43_HAL_PIN_MODE_INPUT (0)
#define CYW43_HAL_PIN_MODE_OUTPUT (1)
#define CYW43_HAL_PIN_PULL_NONE (0)
#define CYW43_HAL_MAC_WLAN0 (0)

/* Point 22 is intentionally silent; the harness reports bounded status. */
#ifndef CYW43_PRINTF
#define CYW43_PRINTF(...) ((void)0)
#endif
#ifndef CYW43_VDEBUG
#define CYW43_VDEBUG(...) ((void)0)
#endif
#ifndef CYW43_DEBUG
#define CYW43_DEBUG(...) ((void)0)
#endif
#ifndef CYW43_INFO
#define CYW43_INFO(...) ((void)0)
#endif
#ifdef CYW43_WARN
#undef CYW43_WARN
#endif
#define CYW43_WARN(...) ((void)0)

#ifdef __cplusplus
extern "C" {
#endif

uint32_t jh_cyw43_port_ticks_us(void);
uint32_t jh_cyw43_port_ticks_ms(void);
void jh_cyw43_port_delay_us(uint32_t delay_us);
void jh_cyw43_port_delay_ms(uint32_t delay_ms);
void jh_cyw43_port_control_wait(void);
void jh_cyw43_port_event_poll(void);
void jh_cyw43_port_get_mac(int interface_index, uint8_t mac[6]);
void jh_cyw43_port_generate_laa_mac(int interface_index, uint8_t mac[6]);
void jh_cyw43_port_pin_config(int pin, int mode, int pull, int alternate);
void jh_cyw43_port_pin_config_irq_falling(int pin, int enabled);
int jh_cyw43_port_pin_read(int pin);
void jh_cyw43_port_pin_low(int pin);
void jh_cyw43_port_pin_high(int pin);
void cyw43_schedule_internal_poll_dispatch(void (*function)(void));

#ifdef __cplusplus
}
#endif

#define cyw43_hal_ticks_us jh_cyw43_port_ticks_us
#define cyw43_hal_ticks_ms jh_cyw43_port_ticks_ms
#define cyw43_delay_us jh_cyw43_port_delay_us
#define cyw43_delay_ms jh_cyw43_port_delay_ms
#define cyw43_hal_get_mac jh_cyw43_port_get_mac
#define cyw43_hal_generate_laa_mac jh_cyw43_port_generate_laa_mac
#define cyw43_hal_pin_config jh_cyw43_port_pin_config
#define cyw43_hal_pin_config_irq_falling jh_cyw43_port_pin_config_irq_falling
#define cyw43_hal_pin_read jh_cyw43_port_pin_read
#define cyw43_hal_pin_low jh_cyw43_port_pin_low
#define cyw43_hal_pin_high jh_cyw43_port_pin_high

#endif
