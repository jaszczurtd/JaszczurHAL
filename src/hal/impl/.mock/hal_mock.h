#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "../../hal_gpio.h"

// ── GPIO ─────────────────────────────────────────────────────────────────────
bool            hal_mock_gpio_get_state(uint8_t pin);
bool            hal_mock_gpio_is_output(uint8_t pin);
hal_gpio_mode_t hal_mock_gpio_get_mode(uint8_t pin);
void            hal_mock_gpio_inject_level(uint8_t pin, bool high);
/** @brief Fire the interrupt callback registered for pin (via hal_gpio_attach_interrupt). */
void            hal_mock_gpio_fire_interrupt(uint8_t pin);
/** @brief Return the GPIO IRQ priority set via hal_gpio_set_irq_priority(). */
hal_irq_priority_t hal_mock_gpio_get_irq_priority(void);

// ── PWM ──────────────────────────────────────────────────────────────────────
uint32_t hal_mock_pwm_get_value(uint8_t pin);
uint8_t  hal_mock_pwm_get_resolution(void);

// ── PWM freq-controlled channels ─────────────────────────────────────────────
#include "../../hal_pwm_freq.h"
int      hal_mock_pwm_freq_get_value(hal_pwm_freq_channel_t ch);
uint32_t hal_mock_pwm_freq_get_frequency(hal_pwm_freq_channel_t ch);
uint8_t  hal_mock_pwm_freq_get_pin(hal_pwm_freq_channel_t ch);

// ── Timer ─────────────────────────────────────────────────────────────────────
void     hal_mock_timer_advance_us(uint64_t us);
uint64_t hal_mock_timer_get_us(void);
void     hal_mock_timer_reset(void);

// ── System ───────────────────────────────────────────────────────────────────
void     hal_mock_set_millis(uint32_t ms);
void     hal_mock_advance_millis(uint32_t ms);
void     hal_mock_set_micros(uint32_t us);
void     hal_mock_advance_micros(uint32_t us);
bool     hal_mock_watchdog_was_fed(void);
void     hal_mock_watchdog_reset_flag(void);
void     hal_mock_set_caused_reboot(bool val);
void     hal_mock_set_free_heap(uint32_t bytes);
/** @brief Inject a chip temperature value returned by hal_read_chip_temp(). Default: 25.0 °C. */
void     hal_mock_set_chip_temp(float celsius);

// ── Serial / Debug ────────────────────────────────────────────────────────────
const char *hal_mock_serial_last_line(void);
const char *hal_mock_deb_last_line(void);
void        hal_mock_serial_reset(void);void        hal_mock_serial_inject_rx(const char *data, int len);
// ── CAN ──────────────────────────────────────────────────────────────────────
#include "../../hal_can.h"
void hal_mock_can_inject(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data);
bool hal_mock_can_get_sent(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data);
void hal_mock_can_reset(hal_can_t h);

// ── ADC ──────────────────────────────────────────────────────────────────────
uint8_t hal_mock_adc_get_resolution(void);
void    hal_mock_adc_inject(uint8_t pin, int value);

// ── SoftwareSerial (swserial) ─────────────────────────────────────────────────
#include "../../hal_swserial.h"
/** @brief Inject bytes into the mock software-serial RX buffer. */
void hal_mock_swserial_push(hal_swserial_t h, const uint8_t *data, int len);
/** @brief Reset RX buffer and last-write capture. */
void hal_mock_swserial_reset(hal_swserial_t h);
/** @brief Return the last string written via hal_swserial_write/println. */
const char *hal_mock_swserial_last_write(hal_swserial_t h);

// ── Hardware UART ─────────────────────────────────────────────────────────────
#include "../../hal_uart.h"
/** @brief Inject bytes into the mock hardware-UART RX buffer. */
void hal_mock_uart_push(hal_uart_t h, const uint8_t *data, int len);
/** @brief Reset RX buffer and last-write capture. */
void hal_mock_uart_reset(hal_uart_t h);
/** @brief Return the last string written via hal_uart_write/println. */
const char *hal_mock_uart_last_write(hal_uart_t h);
/** @brief Return the current RX pin stored in the handle. */
uint8_t hal_mock_uart_get_rx_pin(hal_uart_t h);
/** @brief Return the current TX pin stored in the handle. */
uint8_t hal_mock_uart_get_tx_pin(hal_uart_t h);

// ── SPI ───────────────────────────────────────────────────────────────────────
bool    hal_mock_spi_is_initialized(void);
uint8_t hal_mock_spi_get_bus(void);
uint8_t hal_mock_spi_get_rx_pin(void);
uint8_t hal_mock_spi_get_tx_pin(void);
uint8_t hal_mock_spi_get_sck_pin(void);
int     hal_mock_spi_get_lock_depth(uint8_t bus);
void    hal_mock_spi_reset(void);

// ── RGB LED ───────────────────────────────────────────────────────────────────
#include "../../hal_rgb_led.h"
bool                hal_mock_rgb_led_is_initialized(void);
hal_rgb_led_color_t hal_mock_rgb_led_get_color(void);
uint8_t             hal_mock_rgb_led_get_brightness(void);
hal_rgb_led_pixel_type_t hal_mock_rgb_led_get_pixel_type(void);
uint8_t             hal_mock_rgb_led_get_pin(void);
uint8_t             hal_mock_rgb_led_get_num_pixels(void);
void                hal_mock_rgb_led_reset(void);

// ── Display ───────────────────────────────────────────────────────────────────
#include "../../hal_display.h"
/** @brief Reset all mock display state to defaults. */
void         hal_mock_display_reset(void);
/** @brief Return the last string passed to hal_display_print(). */
const char  *hal_mock_display_last_print(void);
/** @brief Return the last string passed to hal_display_println(). */
const char  *hal_mock_display_last_println(void);
/** @brief Return the current font set via hal_display_set_font(). */
hal_font_id_t hal_mock_display_get_font(void);
/** @brief Return the current text colour set via hal_display_set_text_color(). */
uint16_t     hal_mock_display_get_text_color(void);
/** @brief Return the current text size set via hal_display_set_text_size(). */
uint8_t      hal_mock_display_get_text_size(void);
/** @brief Read the current cursor position. */
void         hal_mock_display_get_cursor(int *x, int *y);
/** @brief Read parameters of the last hal_display_fill_rect() call. */
void         hal_mock_display_get_last_fill_rect(int *x, int *y, int *w, int *h, uint16_t *color);
/** @brief Read parameters of the last hal_display_draw_rgb_bitmap() call. */
void         hal_mock_display_get_last_bitmap(int *x, int *y, uint16_t **data, int *w, int *h);

// ── WiFi ─────────────────────────────────────────────────────────────────────
#include "../../hal_wifi.h"
/** @brief Reset all mock WiFi state to defaults. */
void        hal_mock_wifi_reset(void);
/** @brief Inject the connected flag returned by hal_wifi_is_connected(). */
void        hal_mock_wifi_set_connected(bool connected);
/** @brief Inject the status value returned by hal_wifi_status(). */
void        hal_mock_wifi_set_status(int status);
/** @brief Inject RSSI value returned by hal_wifi_rssi(). */
void        hal_mock_wifi_set_rssi(int32_t rssi);
/** @brief Inject the local IP string returned by hal_wifi_get_local_ip(). */
void        hal_mock_wifi_set_local_ip(const char *ip);
/** @brief Inject the DNS IP string returned by hal_wifi_get_dns_ip(). */
void        hal_mock_wifi_set_dns_ip(const char *ip);
/** @brief Inject the MAC string returned by hal_wifi_get_mac(). */
void        hal_mock_wifi_set_mac(const char *mac);
/** @brief Inject the ping result returned by hal_wifi_ping(). */
void        hal_mock_wifi_set_ping_result(int result);
/** @brief Return the hostname set by hal_wifi_set_hostname(). */
const char *hal_mock_wifi_get_hostname(void);
/** @brief Return the timeout set by hal_wifi_set_timeout_ms(). */
uint32_t    hal_mock_wifi_get_timeout_ms(void);

// ── Time / NTP ──────────────────────────────────────────────────────────────
#include "../../hal_time.h"
/** @brief Reset all mock time state to defaults. */
void        hal_mock_time_reset(void);
/** @brief Inject Unix timestamp returned by hal_time_unix(). */
void        hal_mock_time_set_unix(uint64_t unix_time);
/** @brief Inject local-time breakdown returned by hal_time_get_local(). */
void        hal_mock_time_set_local(const struct tm *tm_local);
/** @brief Return the timezone string set by hal_time_set_timezone(). */
const char *hal_mock_time_get_timezone(void);
/** @brief Return the primary NTP server set by hal_time_sync_ntp(). */
const char *hal_mock_time_get_ntp_primary(void);
/** @brief Return the secondary NTP server set by hal_time_sync_ntp(). */
const char *hal_mock_time_get_ntp_secondary(void);

// ── Thermocouple ──────────────────────────────────────────────────────────────
#include "../../hal_thermocouple.h"
/** @brief Inject the hot-junction temperature returned by hal_thermocouple_read(). */
void hal_mock_thermocouple_set_temp(hal_thermocouple_t h, float temp);
/** @brief Inject the cold-junction temperature returned by hal_thermocouple_read_ambient(). */
void hal_mock_thermocouple_set_ambient(hal_thermocouple_t h, float temp);
/** @brief Inject the raw ADC value returned by hal_thermocouple_read_adc_raw(). */
void hal_mock_thermocouple_set_adc_raw(hal_thermocouple_t h, int32_t raw);
/** @brief Inject the status byte returned by hal_thermocouple_get_status(). */
void hal_mock_thermocouple_set_status(hal_thermocouple_t h, uint8_t status);

// ── GPS ───────────────────────────────────────────────────────────────────

/** @brief Inject latitude and longitude into the mock GPS. */
void hal_mock_gps_set_location(double lat, double lng);
/** @brief Set the mock location-valid flag (returned by hal_gps_location_is_valid()). */
void hal_mock_gps_set_valid(bool valid);
/** @brief Set the mock location-updated flag (returned by hal_gps_location_is_updated()). */
void hal_mock_gps_set_updated(bool updated);
/** @brief Set the mock location age in milliseconds (returned by hal_gps_location_age()). */
void hal_mock_gps_set_age(uint32_t age_ms);
/** @brief Set the mock ground speed in km/h (returned by hal_gps_speed_kmph()). */
void hal_mock_gps_set_speed(double kmph);
/** @brief Set the mock GPS date fields. */
void hal_mock_gps_set_date(int year, int month, int day);
/** @brief Set the mock GPS time fields (UTC). */
void hal_mock_gps_set_time(int hour, int minute, int second);
/** @brief Reset all mock GPS state to zero / invalid. */
void hal_mock_gps_reset(void);

// ── EEPROM ───────────────────────────────────────────────────────────────────
#include "../../hal_eeprom.h"
/** @brief Read a byte directly from the mock EEPROM backing store. */
uint8_t           hal_mock_eeprom_get_byte(uint16_t addr);
/** @brief Return the EEPROM type set by hal_eeprom_init(). */
hal_eeprom_type_t hal_mock_eeprom_get_type(void);
/** @brief Return true if hal_eeprom_commit() has been called since the last reset. */
bool              hal_mock_eeprom_was_committed(void);
/** @brief Clear the committed flag (allows re-checking in tests). */
void              hal_mock_eeprom_clear_committed_flag(void);
/** @brief Return number of byte writes performed since last reset/counter clear. */
uint32_t          hal_mock_eeprom_get_write_count(void);
/** @brief Clear EEPROM byte-write counter. */
void              hal_mock_eeprom_clear_write_count(void);
/** @brief Reset all mock EEPROM state (memory, type, committed flag) to defaults. */
void              hal_mock_eeprom_reset(void);

// ── I2C ──────────────────────────────────────────────────────────────────────

/**
 * @brief Pre-load the receive buffer used by hal_i2c_read().
 * @param data Pointer to the byte array to inject.
 * @param len  Number of bytes (clamped to MOCK_I2C_BUF_SIZE = 256).
 */
void    hal_mock_i2c_inject_rx(const uint8_t *data, int len);
/** @brief Pre-load receive buffer for selected I2C mock bus (0 = Wire, 1 = Wire1). */
void    hal_mock_i2c_inject_rx_bus(uint8_t bus, const uint8_t *data, int len);
/** @brief Return the 7-bit address of the last hal_i2c_begin_transmission() call. */
uint8_t hal_mock_i2c_get_last_addr(void);
/** @brief Return the last transmission address for selected I2C mock bus. */
uint8_t hal_mock_i2c_get_last_addr_bus(uint8_t bus);
/** @brief Return the current lock depth for selected I2C mock bus. */
int     hal_mock_i2c_get_lock_depth_bus(uint8_t bus);
/** @brief Return lock depth for default I2C bus (0 = Wire). */
int     hal_mock_i2c_get_lock_depth(void);
/** @brief Return true when selected I2C mock bus is marked initialized. */
bool    hal_mock_i2c_is_initialized_bus(uint8_t bus);
/** @brief Return initialized flag for default I2C bus (0 = Wire). */
bool    hal_mock_i2c_is_initialized(void);
/** @brief Control the value returned by hal_i2c_is_busy() (default: false). */
void    hal_mock_i2c_set_busy(bool busy);
/** @brief Control busy state for selected I2C mock bus (0 = Wire, 1 = Wire1). */
void    hal_mock_i2c_set_busy_bus(uint8_t bus, bool busy);
/** @brief Return how many times hal_i2c_bus_clear() was called on bus 0. */
uint32_t hal_mock_i2c_get_bus_clear_count(void);
/** @brief Return how many times hal_i2c_bus_clear_bus() was called on the given bus. */
uint32_t hal_mock_i2c_get_bus_clear_count_bus(uint8_t bus);

// ── I2C Slave ────────────────────────────────────────────────────────────────
#include "../../hal_i2c_slave.h"

/** @brief Return true if I2C slave on default bus is initialized. */
bool    hal_mock_i2c_slave_is_initialized(void);
/** @brief Return true if I2C slave on selected bus is initialized. */
bool    hal_mock_i2c_slave_is_initialized_bus(uint8_t bus);
/** @brief Read a register directly from the mock slave map (bus 0). */
uint8_t hal_mock_i2c_slave_get_reg(uint8_t reg);
/** @brief Read a register directly from the mock slave map (selected bus). */
uint8_t hal_mock_i2c_slave_get_reg_bus(uint8_t bus, uint8_t reg);
/** @brief Write a register directly into the mock slave map (bus 0). */
void    hal_mock_i2c_slave_set_reg(uint8_t reg, uint8_t value);
/** @brief Write a register directly into the mock slave map (selected bus). */
void    hal_mock_i2c_slave_set_reg_bus(uint8_t bus, uint8_t reg, uint8_t value);
/** @brief Return the current register pointer (bus 0). */
uint8_t hal_mock_i2c_slave_get_reg_ptr(void);
/** @brief Return the current register pointer (selected bus). */
uint8_t hal_mock_i2c_slave_get_reg_ptr_bus(uint8_t bus);
/** @brief Simulate a master-write: first byte = reg pointer, rest = data. */
void    hal_mock_i2c_slave_simulate_receive(const uint8_t *data, int len);
/** @brief Simulate a master-write on selected bus. */
void    hal_mock_i2c_slave_simulate_receive_bus(uint8_t bus, const uint8_t *data, int len);
/** @brief Simulate a master-read starting from current reg pointer. Returns bytes copied. */
int     hal_mock_i2c_slave_simulate_request(uint8_t *out_buf, int max_len);
/** @brief Simulate a master-read on selected bus. */
int     hal_mock_i2c_slave_simulate_request_bus(uint8_t bus, uint8_t *out_buf, int max_len);

// ── External ADC (ADS1115) ───────────────────────────────────────────────────

/** @brief Inject a raw 16-bit ADC result for the given channel (0–3). */
void  hal_mock_ext_adc_inject_raw(uint8_t channel, int16_t value);
/** @brief Inject a pre-scaled float result for the given channel (0–3). */
void  hal_mock_ext_adc_inject_scaled(uint8_t channel, float value);
/** @brief Return the adc_range value set by hal_ext_adc_init(). */
float hal_mock_ext_adc_get_range(void);
