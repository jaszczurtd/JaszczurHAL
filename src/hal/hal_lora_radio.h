#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_LORA

/**
 * @file hal_lora_radio.h
 * @brief Provider-neutral raw LoRa radio API.
 *
 * The MVP exposes SX1262 hardware through an opaque handle. Applications own
 * radio configuration, while the HAL owns per-instance packet buffers and
 * runtime state. SPI controller initialization remains an application-level
 * responsibility and destroying a radio never deinitializes the shared bus.
 */

#include "hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sentinel used for an optional, unconnected radio GPIO. */
#define HAL_LORA_PIN_NONE UINT8_MAX

/** @brief Safe SPI transaction clock selected when config clock is zero. */
#define HAL_LORA_SPI_CLOCK_DEFAULT_HZ UINT32_C(8000000)

/** @brief Maximum SX126x LoRa packet payload owned by one radio handle. */
#define HAL_LORA_RADIO_MAX_PAYLOAD 255u

/** @brief Opaque raw LoRa radio handle. */
typedef struct hal_lora_radio_impl_s *hal_lora_radio_t;

/** @brief Radio models with an implemented provider. */
typedef enum {
  HAL_LORA_RADIO_SX1262 = 0,
} hal_lora_radio_model_t;

/** @brief SX126x RF switch control topology. */
typedef enum {
  HAL_LORA_RF_SWITCH_NONE = 0,
  HAL_LORA_RF_SWITCH_DIO2,
  HAL_LORA_RF_SWITCH_SINGLE_GPIO,
  HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO,
  HAL_LORA_RF_SWITCH_DUAL_GPIO,
} hal_lora_rf_switch_mode_t;

/** @brief SX126x internal regulator selection. */
typedef enum {
  HAL_LORA_REGULATOR_LDO = 0,
  HAL_LORA_REGULATOR_DCDC,
} hal_lora_regulator_mode_t;

/** @brief Discrete DIO3-controlled TCXO voltage supported by SX126x. */
typedef enum {
  HAL_LORA_TCXO_1V6 = 0,
  HAL_LORA_TCXO_1V7,
  HAL_LORA_TCXO_1V8,
  HAL_LORA_TCXO_2V2,
  HAL_LORA_TCXO_2V4,
  HAL_LORA_TCXO_2V7,
  HAL_LORA_TCXO_3V0,
  HAL_LORA_TCXO_3V3,
} hal_lora_tcxo_voltage_t;

/** @brief SX126x TCXO control source. */
typedef enum {
  HAL_LORA_TCXO_CONTROL_NONE = 0,
  HAL_LORA_TCXO_CONTROL_DIO3,
} hal_lora_tcxo_control_t;

/** @brief Module-specific SX126x wiring and electrical limits. */
typedef struct {
  uint8_t reset_pin;
  uint8_t dio1_pin;
  uint8_t busy_pin;

  hal_lora_rf_switch_mode_t rf_switch_mode;
  uint8_t rf_switch_pin_a;
  uint8_t rf_switch_pin_b;
  bool rf_switch_idle_level_a;
  bool rf_switch_idle_level_b;
  bool rf_switch_rx_level_a;
  bool rf_switch_rx_level_b;
  bool rf_switch_tx_level_a;
  bool rf_switch_tx_level_b;

  hal_lora_regulator_mode_t regulator_mode;
  hal_lora_tcxo_control_t tcxo_control;
  hal_lora_tcxo_voltage_t tcxo_voltage;
  uint32_t tcxo_startup_us;
  uint32_t min_frequency_hz;
  uint32_t max_frequency_hz;
  uint32_t max_spi_clock_hz;
  int8_t min_tx_power_dbm;
  int8_t max_tx_power_dbm;
} hal_lora_sx126x_hardware_config_t;

/** @brief Complete provider-neutral radio construction descriptor. */
typedef struct {
  hal_lora_radio_model_t model;

  uint8_t spi_bus;
  uint8_t spi_miso_pin;
  uint8_t spi_mosi_pin;
  uint8_t spi_sck_pin;
  uint8_t cs_pin;
  uint32_t spi_clock_hz;

  union {
    hal_lora_sx126x_hardware_config_t sx126x;
  } hardware;
} hal_lora_radio_config_t;

/** @brief Raw LoRa modem and packet configuration. */
typedef struct {
  uint32_t frequency_hz;
  uint32_t bandwidth_hz;
  uint8_t spreading_factor;
  uint8_t coding_rate;
  int8_t tx_power_dbm;
  uint16_t preamble_symbols;
  uint8_t sync_word;

  bool explicit_header;
  uint8_t implicit_payload_length;
  bool crc_enabled;
  bool invert_iq;
} hal_lora_modem_config_t;

/** @brief Metadata copied with one received packet. */
typedef struct {
  int16_t rssi_dbm;
  int8_t snr_db;
  int16_t signal_rssi_dbm;
  uint32_t timestamp_ms;
  bool crc_valid;
} hal_lora_packet_info_t;

/** @brief Basic per-handle counters and the most recent radio error. */
typedef struct {
  uint32_t transmitted_packets;
  uint32_t received_packets;
  uint32_t crc_errors;
  uint32_t tx_timeouts;
  uint32_t rx_timeouts;
  uint32_t dropped_packets;
  uint32_t bus_errors;
  uint32_t resets;
  int16_t last_rssi_dbm;
  int8_t last_snr_db;
  hal_status_t last_error;
} hal_lora_radio_diagnostics_t;

/** @brief Stable public radio operating states for the MVP. */
typedef enum {
  HAL_LORA_RADIO_STATE_STANDBY = 0,
  HAL_LORA_RADIO_STATE_RX,
  HAL_LORA_RADIO_STATE_TX,
  HAL_LORA_RADIO_STATE_CAD,
  HAL_LORA_RADIO_STATE_SLEEP,
  HAL_LORA_RADIO_STATE_ERROR,
} hal_lora_radio_state_t;

/**
 * @brief Build a radio descriptor from the active board profile.
 * @return HAL_OK or HAL_EUNSUPPORTED when the board has no embedded radio.
 */
hal_status_t
hal_lora_radio_config_from_board(hal_lora_radio_config_t *out_config);

/**
 * @brief Fill the fixed electrical profile of a Waveshare Core1262-HF.
 *
 * The caller still supplies the host SPI bus and all host pin assignments in
 * hal_lora_radio_config_t. Assign RXEN to rf_switch_pin_a and TXEN to
 * rf_switch_pin_b. The helper fills only module-owned electrical and RF
 * limits.
 */
hal_status_t hal_lora_sx126x_core1262_hf_defaults(
    hal_lora_sx126x_hardware_config_t *out_hardware);

/**
 * @brief Create and probe one radio instance using an initialized SPI bus.
 * @return HAL_OK, an argument/configuration error, HAL_ENOMEM when the static
 *         pool is full, or a provider/backend status.
 */
hal_status_t hal_lora_radio_create(const hal_lora_radio_config_t *config,
                                   hal_lora_radio_t *out_radio);

/**
 * @brief Destroy a radio handle without deinitializing its shared SPI bus.
 * @return HAL_OK or HAL_EUNINIT for an invalid or stale handle.
 */
hal_status_t hal_lora_radio_destroy(hal_lora_radio_t radio);

/** @brief Configure raw LoRa modulation and packet parameters. */
hal_status_t hal_lora_radio_configure(hal_lora_radio_t radio,
                                      const hal_lora_modem_config_t *config);

/** @brief Return the balanced EU868 technical preset for an HF module. */
hal_lora_modem_config_t hal_lora_default_eu868(void);

/** @brief Return the long-range EU868 technical preset for an HF module. */
hal_lora_modem_config_t hal_lora_default_long_range_eu868(void);

/** @brief Return the fast EU868 technical preset for an HF module. */
hal_lora_modem_config_t hal_lora_default_fast_eu868(void);

/**
 * @brief Transmit one copied raw packet and wait for completion.
 * @note A preset does not by itself guarantee regional regulatory compliance.
 */
hal_status_t hal_lora_radio_transmit(hal_lora_radio_t radio,
                                     const uint8_t *data, size_t length,
                                     uint32_t timeout_ms);

/** @brief Start bounded receive mode; poll with hal_lora_radio_receive(). */
hal_status_t hal_lora_radio_receive_start(hal_lora_radio_t radio,
                                          uint32_t timeout_ms);

/** @brief Start continuous receive mode without a magic timeout value. */
hal_status_t hal_lora_radio_receive_start_continuous(hal_lora_radio_t radio);

/**
 * @brief Poll for and copy one received packet from the handle-owned buffer.
 *
 * HAL_EAGAIN means reception is still in progress. On HAL_EOVERFLOW,
 * out_length reports the complete packet length and at most buffer_size bytes
 * are copied before the packet is consumed. out_info may be NULL.
 */
hal_status_t hal_lora_radio_receive(hal_lora_radio_t radio, uint8_t *buffer,
                                    size_t buffer_size, size_t *out_length,
                                    hal_lora_packet_info_t *out_info);

/** @brief Read the current stable public radio state. */
hal_status_t hal_lora_radio_get_state(hal_lora_radio_t radio,
                                      hal_lora_radio_state_t *out_state);

/** @brief Copy basic counters and the most recent error for one handle. */
hal_status_t
hal_lora_radio_get_diagnostics(hal_lora_radio_t radio,
                               hal_lora_radio_diagnostics_t *out_diagnostics);

/** @brief Enter the provider's low-power sleep state. */
hal_status_t hal_lora_radio_sleep(hal_lora_radio_t radio);

/** @brief Return the radio to its standby state. */
hal_status_t hal_lora_radio_standby(hal_lora_radio_t radio);

/** @brief Calculate rounded-up LoRa packet airtime in milliseconds. */
hal_status_t hal_lora_time_on_air(const hal_lora_modem_config_t *config,
                                  size_t payload_length, uint32_t *out_time_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_LORA */
