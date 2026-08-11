#include "jh_sx126x_adapter.h"

#ifdef HAL_ENABLE_SX126X

#include "hal/gpio/hal_gpio.h"
#include "hal/spi/hal_spi.h"
#include "hal/system/hal_system.h"

#include "sx126x.h"

#include <limits.h>
#include <string.h>

#define JH_SX126X_OPCODE_SET_SLEEP 0x84u
#define JH_SX126X_OPCODE_GET_STATUS 0xC0u
#define JH_SX126X_RESET_LOW_US UINT32_C(200)
#define JH_SX126X_RESET_SETTLE_US UINT32_C(10000)

static volatile bool s_dio1_pending = false;

static void sx126x_dio1_interrupt(void) { s_dio1_pending = true; }

typedef enum {
  JH_SX126X_RF_IDLE,
  JH_SX126X_RF_RX,
  JH_SX126X_RF_TX,
} jh_sx126x_rf_path_t;

static jh_lora_radio_context_t *mutable_context(const void *context) {
  return const_cast<jh_lora_radio_context_t *>(
      static_cast<const jh_lora_radio_context_t *>(context));
}

static int16_t signed_byte_to_int16(int8_t value) {
  const uint8_t bits = static_cast<uint8_t>(value);
  if ((bits & 0x80u) == 0u) {
    return static_cast<int16_t>(bits);
  }
  return static_cast<int16_t>(static_cast<int>(bits) - 256);
}

static uint32_t spi_clock(const jh_lora_radio_context_t *context) {
  return context->config.spi_clock_hz == 0u ? HAL_LORA_SPI_CLOCK_DEFAULT_HZ
                                            : context->config.spi_clock_hz;
}

static void store_transport_status(jh_lora_radio_context_t *context,
                                   hal_status_t status) {
  context->provider_last_status = status;
}

hal_status_t jh_sx126x_wait_while_busy(jh_lora_radio_context_t *context,
                                       uint32_t timeout_ms) {
  if (context == NULL) {
    return HAL_EINVAL;
  }
  const uint64_t started_us = hal_micros64();
  const uint64_t timeout_us = (uint64_t)timeout_ms * UINT64_C(1000);
  while (hal_gpio_read(context->config.hardware.sx126x.busy_pin)) {
    if (hal_micros64() - started_us >= timeout_us) {
      store_transport_status(context, HAL_ETIMEOUT);
      return HAL_ETIMEOUT;
    }
    hal_delay_us(100u);
  }
  return HAL_OK;
}

static hal_status_t transaction_begin(jh_lora_radio_context_t *context) {
  const uint8_t bus = context->config.spi_bus;
  const hal_spi_settings_t settings = {
      spi_clock(context),
      HAL_SPI_MSBFIRST,
      HAL_SPI_MODE0,
  };
  hal_spi_lock(bus);
  const hal_status_t status = hal_spi_begin_transaction(bus, &settings);
  if (status != HAL_OK) {
    hal_spi_unlock(bus);
    store_transport_status(context, HAL_EBUS);
    return HAL_EBUS;
  }
  hal_gpio_write(context->config.cs_pin, false);
  return HAL_OK;
}

static hal_status_t transaction_end(jh_lora_radio_context_t *context,
                                    hal_status_t transfer_status) {
  const uint8_t bus = context->config.spi_bus;
  hal_gpio_write(context->config.cs_pin, true);
  const hal_status_t end_status = hal_spi_end_transaction(bus);
  hal_spi_unlock(bus);
  const hal_status_t status = transfer_status != HAL_OK
                                  ? transfer_status
                                  : (end_status == HAL_OK ? HAL_OK : HAL_EBUS);
  store_transport_status(context, status);
  return status;
}

static hal_status_t write_transaction(jh_lora_radio_context_t *context,
                                      const uint8_t *command,
                                      uint16_t command_length,
                                      const uint8_t *data,
                                      uint16_t data_length) {
  hal_status_t status = transaction_begin(context);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_spi_write(context->config.spi_bus, command, command_length);
  if (status == HAL_OK && data_length > 0u) {
    status = hal_spi_write(context->config.spi_bus, data, data_length);
  }
  return transaction_end(context, status == HAL_OK ? HAL_OK : HAL_EBUS);
}

static hal_status_t read_transaction(jh_lora_radio_context_t *context,
                                     const uint8_t *command,
                                     uint16_t command_length, uint8_t *data,
                                     uint16_t data_length) {
  hal_status_t status = transaction_begin(context);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_spi_write(context->config.spi_bus, command, command_length);
  for (uint16_t index = 0u; status == HAL_OK && index < data_length; ++index) {
    status =
        hal_spi_transfer_ex(context->config.spi_bus, SX126X_NOP, &data[index]);
  }
  return transaction_end(context, status == HAL_OK ? HAL_OK : HAL_EBUS);
}

static void set_rf_path(jh_lora_radio_context_t *context,
                        jh_sx126x_rf_path_t path) {
  const hal_lora_sx126x_hardware_config_t *hardware =
      &context->config.hardware.sx126x;
  if (hardware->rf_switch_pin_a != HAL_LORA_PIN_NONE) {
    const bool level =
        path == JH_SX126X_RF_TX
            ? hardware->rf_switch_tx_level_a
            : (path == JH_SX126X_RF_RX ? hardware->rf_switch_rx_level_a
                                       : hardware->rf_switch_idle_level_a);
    hal_gpio_write(hardware->rf_switch_pin_a, level);
  }
  if (hardware->rf_switch_pin_b != HAL_LORA_PIN_NONE) {
    const bool level =
        path == JH_SX126X_RF_TX
            ? hardware->rf_switch_tx_level_b
            : (path == JH_SX126X_RF_RX ? hardware->rf_switch_rx_level_b
                                       : hardware->rf_switch_idle_level_b);
    hal_gpio_write(hardware->rf_switch_pin_b, level);
  }
}

void jh_sx126x_set_rf_idle(jh_lora_radio_context_t *context) {
  set_rf_path(context, JH_SX126X_RF_IDLE);
}

void jh_sx126x_set_rf_rx(jh_lora_radio_context_t *context) {
  set_rf_path(context, JH_SX126X_RF_RX);
}

void jh_sx126x_set_rf_tx(jh_lora_radio_context_t *context) {
  set_rf_path(context, JH_SX126X_RF_TX);
}

extern "C" sx126x_hal_status_t sx126x_hal_write(const void *opaque,
                                                const uint8_t *command,
                                                uint16_t command_length,
                                                const uint8_t *data,
                                                uint16_t data_length) {
  jh_lora_radio_context_t *context = mutable_context(opaque);
  if (context == NULL || command == NULL || command_length == 0u ||
      (data_length > 0u && data == NULL)) {
    if (context != NULL) {
      store_transport_status(context, HAL_EINVAL);
    }
    return SX126X_HAL_STATUS_ERROR;
  }
  if (context->provider_sleeping) {
    if (sx126x_hal_wakeup(context) != SX126X_HAL_STATUS_OK) {
      return SX126X_HAL_STATUS_ERROR;
    }
  }
  if (jh_sx126x_wait_while_busy(context, HAL_LORA_SX126X_BUSY_TIMEOUT_MS) !=
      HAL_OK) {
    return SX126X_HAL_STATUS_ERROR;
  }
  if (write_transaction(context, command, command_length, data, data_length) !=
      HAL_OK) {
    return SX126X_HAL_STATUS_ERROR;
  }
  if (command[0] == JH_SX126X_OPCODE_SET_SLEEP) {
    context->provider_sleeping = true;
    hal_delay_us(500u);
    return SX126X_HAL_STATUS_OK;
  }
  if (jh_sx126x_wait_while_busy(context, HAL_LORA_SX126X_BUSY_TIMEOUT_MS) !=
      HAL_OK) {
    return SX126X_HAL_STATUS_ERROR;
  }
  store_transport_status(context, HAL_OK);
  return SX126X_HAL_STATUS_OK;
}

extern "C" sx126x_hal_status_t
sx126x_hal_read(const void *opaque, const uint8_t *command,
                uint16_t command_length, uint8_t *data, uint16_t data_length) {
  jh_lora_radio_context_t *context = mutable_context(opaque);
  if (context == NULL || command == NULL || command_length == 0u ||
      (data_length > 0u && data == NULL)) {
    if (context != NULL) {
      store_transport_status(context, HAL_EINVAL);
    }
    return SX126X_HAL_STATUS_ERROR;
  }
  if (context->provider_sleeping &&
      sx126x_hal_wakeup(context) != SX126X_HAL_STATUS_OK) {
    return SX126X_HAL_STATUS_ERROR;
  }
  if (jh_sx126x_wait_while_busy(context, HAL_LORA_SX126X_BUSY_TIMEOUT_MS) !=
      HAL_OK) {
    return SX126X_HAL_STATUS_ERROR;
  }
  if (read_transaction(context, command, command_length, data, data_length) !=
      HAL_OK) {
    return SX126X_HAL_STATUS_ERROR;
  }
  store_transport_status(context, HAL_OK);
  return SX126X_HAL_STATUS_OK;
}

extern "C" sx126x_hal_status_t sx126x_hal_reset(const void *opaque) {
  jh_lora_radio_context_t *context = mutable_context(opaque);
  if (context == NULL) {
    return SX126X_HAL_STATUS_ERROR;
  }
  const uint8_t reset_pin = context->config.hardware.sx126x.reset_pin;
  hal_gpio_write(reset_pin, false);
  hal_delay_us(JH_SX126X_RESET_LOW_US);
  hal_gpio_write(reset_pin, true);
  hal_delay_us(JH_SX126X_RESET_SETTLE_US);
  context->provider_sleeping = false;
  const hal_status_t status =
      jh_sx126x_wait_while_busy(context, HAL_LORA_SX126X_BUSY_TIMEOUT_MS);
  store_transport_status(context, status);
  return status == HAL_OK ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

extern "C" sx126x_hal_status_t sx126x_hal_wakeup(const void *opaque) {
  jh_lora_radio_context_t *context = mutable_context(opaque);
  if (context == NULL) {
    return SX126X_HAL_STATUS_ERROR;
  }
  const uint8_t command[] = {JH_SX126X_OPCODE_GET_STATUS, SX126X_NOP};
  if (write_transaction(context, command, sizeof(command), NULL, 0u) !=
      HAL_OK) {
    return SX126X_HAL_STATUS_ERROR;
  }
  const hal_status_t status =
      jh_sx126x_wait_while_busy(context, HAL_LORA_SX126X_BUSY_TIMEOUT_MS);
  if (status == HAL_OK) {
    context->provider_sleeping = false;
  }
  store_transport_status(context, status);
  return status == HAL_OK ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

static hal_status_t map_status(jh_lora_radio_context_t *context,
                               sx126x_status_t status) {
  if (status == SX126X_STATUS_OK) {
    return HAL_OK;
  }
  if (context->provider_last_status != HAL_OK &&
      context->provider_last_status != HAL_NONE) {
    return context->provider_last_status;
  }
  if (status == SX126X_STATUS_UNSUPPORTED_FEATURE) {
    return HAL_EUNSUPPORTED;
  }
  if (status == SX126X_STATUS_UNKNOWN_VALUE) {
    return HAL_EINVAL;
  }
  return HAL_EIO;
}

static bool bandwidth_to_sx126x(uint32_t bandwidth_hz,
                                sx126x_lora_bw_t *out_bandwidth) {
  if (out_bandwidth == NULL) {
    return false;
  }
  switch (bandwidth_hz) {
  case 7800u:
  case 7812u:
    *out_bandwidth = SX126X_LORA_BW_007;
    return true;
  case 10400u:
  case 10417u:
    *out_bandwidth = SX126X_LORA_BW_010;
    return true;
  case 15600u:
  case 15625u:
    *out_bandwidth = SX126X_LORA_BW_015;
    return true;
  case 20800u:
  case 20833u:
    *out_bandwidth = SX126X_LORA_BW_020;
    return true;
  case 31250u:
    *out_bandwidth = SX126X_LORA_BW_031;
    return true;
  case 41700u:
  case 41667u:
    *out_bandwidth = SX126X_LORA_BW_041;
    return true;
  case 62500u:
    *out_bandwidth = SX126X_LORA_BW_062;
    return true;
  case 125000u:
    *out_bandwidth = SX126X_LORA_BW_125;
    return true;
  case 250000u:
    *out_bandwidth = SX126X_LORA_BW_250;
    return true;
  case 500000u:
    *out_bandwidth = SX126X_LORA_BW_500;
    return true;
  default:
    return false;
  }
}

bool jh_sx126x_modem_config_valid(
    const hal_lora_modem_config_t *config,
    const hal_lora_sx126x_hardware_config_t *hardware) {
  sx126x_lora_bw_t bandwidth;
  return config != NULL && hardware != NULL &&
         config->frequency_hz >= hardware->min_frequency_hz &&
         config->frequency_hz <= hardware->max_frequency_hz &&
         bandwidth_to_sx126x(config->bandwidth_hz, &bandwidth) &&
         config->spreading_factor >= 5u && config->spreading_factor <= 12u &&
         config->coding_rate >= 5u && config->coding_rate <= 8u &&
         config->tx_power_dbm >= hardware->min_tx_power_dbm &&
         config->tx_power_dbm <= hardware->max_tx_power_dbm &&
         config->preamble_symbols > 0u &&
         (config->explicit_header || config->implicit_payload_length > 0u);
}

static bool build_modulation(const hal_lora_modem_config_t *config,
                             sx126x_mod_params_lora_t *out_modulation) {
  sx126x_lora_bw_t bandwidth;
  if (config == NULL || out_modulation == NULL ||
      !bandwidth_to_sx126x(config->bandwidth_hz, &bandwidth) ||
      config->spreading_factor < 5u || config->spreading_factor > 12u ||
      config->coding_rate < 5u || config->coding_rate > 8u) {
    return false;
  }
  out_modulation->sf = static_cast<sx126x_lora_sf_t>(config->spreading_factor);
  out_modulation->bw = bandwidth;
  out_modulation->cr = static_cast<sx126x_lora_cr_t>(config->coding_rate - 4u);
  const uint64_t symbol_us =
      (((UINT64_C(1) << config->spreading_factor) * UINT64_C(1000000)) +
       config->bandwidth_hz - 1u) /
      config->bandwidth_hz;
  out_modulation->ldro = symbol_us >= UINT64_C(16000) ? 1u : 0u;
  return true;
}

static bool build_packet(const hal_lora_modem_config_t *config,
                         size_t payload_length,
                         sx126x_pkt_params_lora_t *out_packet) {
  if (config == NULL || out_packet == NULL || payload_length > UINT8_MAX ||
      config->preamble_symbols == 0u ||
      (!config->explicit_header &&
       (config->implicit_payload_length == 0u ||
        payload_length != config->implicit_payload_length))) {
    return false;
  }
  out_packet->preamble_len_in_symb = config->preamble_symbols;
  out_packet->header_type = config->explicit_header ? SX126X_LORA_PKT_EXPLICIT
                                                    : SX126X_LORA_PKT_IMPLICIT;
  out_packet->pld_len_in_bytes = config->explicit_header
                                     ? static_cast<uint8_t>(payload_length)
                                     : config->implicit_payload_length;
  out_packet->crc_is_on = config->crc_enabled;
  out_packet->invert_iq_is_on = config->invert_iq;
  return true;
}

hal_status_t jh_sx126x_time_on_air(const hal_lora_modem_config_t *config,
                                   size_t payload_length,
                                   uint32_t *out_time_ms) {
  if (out_time_ms != NULL) {
    *out_time_ms = 0u;
  }
  sx126x_mod_params_lora_t modulation = {};
  sx126x_pkt_params_lora_t packet = {};
  if (out_time_ms == NULL || !build_modulation(config, &modulation) ||
      !build_packet(config, payload_length, &packet)) {
    return HAL_EINVAL;
  }
  *out_time_ms = sx126x_get_lora_time_on_air_in_ms(&packet, &modulation);
  return *out_time_ms > 0u ? HAL_OK : HAL_EINVAL;
}

static hal_status_t configure_packet(jh_lora_radio_context_t *context,
                                     size_t payload_length) {
  sx126x_pkt_params_lora_t packet = {};
  if (!build_packet(&context->modem, payload_length, &packet)) {
    return HAL_EINVAL;
  }
  context->provider_last_status = HAL_OK;
  return map_status(context, sx126x_set_lora_pkt_params(context, &packet));
}

static hal_status_t sx126x_initialize(jh_lora_radio_context_t *context) {
  const hal_lora_sx126x_hardware_config_t *hardware =
      &context->config.hardware.sx126x;
  const hal_status_t spi_status =
      hal_spi_init(context->config.spi_bus, context->config.spi_miso_pin,
                   context->config.spi_mosi_pin, context->config.spi_sck_pin);
  if (spi_status != HAL_OK) {
    store_transport_status(context, HAL_EBUS);
    return HAL_EBUS;
  }
  hal_gpio_set_mode(context->config.cs_pin, HAL_GPIO_OUTPUT_HIGH);
  hal_gpio_set_mode(hardware->reset_pin, HAL_GPIO_OUTPUT_HIGH);
  hal_gpio_set_mode(hardware->busy_pin, HAL_GPIO_INPUT);
  hal_gpio_set_mode(hardware->dio1_pin, HAL_GPIO_INPUT);
  if (hardware->rf_switch_pin_a != HAL_LORA_PIN_NONE) {
    hal_gpio_set_mode(hardware->rf_switch_pin_a,
                      hardware->rf_switch_idle_level_a ? HAL_GPIO_OUTPUT_HIGH
                                                       : HAL_GPIO_OUTPUT_LOW);
  }
  if (hardware->rf_switch_pin_b != HAL_LORA_PIN_NONE) {
    hal_gpio_set_mode(hardware->rf_switch_pin_b,
                      hardware->rf_switch_idle_level_b ? HAL_GPIO_OUTPUT_HIGH
                                                       : HAL_GPIO_OUTPUT_LOW);
  }
  jh_sx126x_set_rf_idle(context);
  context->provider_last_status = HAL_OK;
  hal_status_t status = map_status(context, sx126x_reset(context));
  if (status != HAL_OK) {
    return status;
  }
  status =
      map_status(context, sx126x_set_standby(context, SX126X_STANDBY_CFG_RC));
  if (status != HAL_OK) {
    return status;
  }
  if (hardware->tcxo_control == HAL_LORA_TCXO_CONTROL_DIO3) {
    sx126x_errors_mask_t device_errors = 0u;
    status =
        map_status(context, sx126x_get_device_errors(context, &device_errors));
    if (status == HAL_OK && (device_errors & SX126X_ERRORS_XOSC_START) != 0u) {
      status = map_status(context, sx126x_clear_device_errors(context));
    }
    if (status != HAL_OK) {
      return status;
    }
    const uint64_t steps64 =
        ((uint64_t)hardware->tcxo_startup_us * UINT64_C(64) + 999u) / 1000u;
    if (steps64 == 0u || steps64 > SX126X_MAX_TIMEOUT_IN_RTC_STEP) {
      return HAL_EINVAL;
    }
    status = map_status(context, sx126x_set_dio3_as_tcxo_ctrl(
                                     context,
                                     static_cast<sx126x_tcxo_ctrl_voltages_t>(
                                         hardware->tcxo_voltage),
                                     static_cast<uint32_t>(steps64)));
    if (status != HAL_OK) {
      return status;
    }
  }
  status = map_status(
      context, sx126x_set_reg_mode(context, hardware->regulator_mode ==
                                                    HAL_LORA_REGULATOR_DCDC
                                                ? SX126X_REG_MODE_DCDC
                                                : SX126X_REG_MODE_LDO));
  if (status != HAL_OK) {
    return status;
  }
  const bool dio2 =
      hardware->rf_switch_mode == HAL_LORA_RF_SWITCH_DIO2 ||
      hardware->rf_switch_mode == HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO;
  status = map_status(context, sx126x_set_dio2_as_rf_sw_ctrl(context, dio2));
  if (status != HAL_OK) {
    return status;
  }
  const sx126x_pa_cfg_params_t pa = {0x04u, 0x07u, 0x00u, 0x01u};
  status = map_status(context, sx126x_set_pa_cfg(context, &pa));
  if (status == HAL_OK) {
    status = map_status(
        context, sx126x_set_ocp_value(context, SX126X_OCP_PARAM_VALUE_140_MA));
  }
  if (status == HAL_OK) {
    status = map_status(context, sx126x_cfg_tx_clamp(context));
  }
  if (status == HAL_OK) {
    status = map_status(context, sx126x_set_rx_tx_fallback_mode(
                                     context, SX126X_FALLBACK_STDBY_RC));
  }
  if (status == HAL_OK) {
    status =
        map_status(context, sx126x_set_buffer_base_address(context, 0u, 0u));
  }
  if (status == HAL_OK) {
    const sx126x_cal_mask_t calibration =
        SX126X_CAL_RC64K | SX126X_CAL_RC13M | SX126X_CAL_PLL |
        SX126X_CAL_ADC_PULSE | SX126X_CAL_ADC_BULK_N | SX126X_CAL_ADC_BULK_P;
    status = map_status(context, sx126x_cal(context, calibration));
  }
  sx126x_chip_status_t chip_status = {};
  if (status == HAL_OK) {
    status = map_status(context, sx126x_get_status(context, &chip_status));
  }
  if (status == HAL_OK && (chip_status.chip_mode == SX126X_CHIP_MODE_UNUSED ||
                           chip_status.chip_mode == SX126X_CHIP_MODE_RFU)) {
    status = HAL_EHW;
  }
  if (status != HAL_OK) {
    jh_sx126x_set_rf_idle(context);
  } else {
    hal_gpio_attach_interrupt(hardware->dio1_pin, sx126x_dio1_interrupt,
                              HAL_GPIO_IRQ_RISING);
    context->provider_irq_attached = true;
  }
  return status;
}

static hal_status_t sx126x_deinitialize(jh_lora_radio_context_t *context) {
  if (context->provider_irq_attached) {
    hal_gpio_detach_interrupt(context->config.hardware.sx126x.dio1_pin);
    context->provider_irq_attached = false;
  }
  jh_sx126x_set_rf_idle(context);
  if (context->provider_sleeping) {
    const hal_status_t wake = map_status(context, sx126x_wakeup(context));
    if (wake != HAL_OK) {
      return wake;
    }
  }
  const hal_status_t status =
      map_status(context, sx126x_set_standby(context, SX126X_STANDBY_CFG_RC));
  hal_gpio_write(context->config.cs_pin, true);
  return status;
}

static hal_status_t sx126x_configure(jh_lora_radio_context_t *context) {
  sx126x_mod_params_lora_t modulation = {};
  if (!build_modulation(&context->modem, &modulation)) {
    return HAL_EINVAL;
  }
  hal_status_t status =
      map_status(context, sx126x_set_pkt_type(context, SX126X_PKT_TYPE_LORA));
  if (status == HAL_OK) {
    const uint16_t frequency_mhz =
        static_cast<uint16_t>(context->modem.frequency_hz / UINT32_C(1000000));
    uint8_t calibration_lower = static_cast<uint8_t>(
        frequency_mhz / SX126X_IMAGE_CALIBRATION_STEP_IN_MHZ);
    uint8_t calibration_upper = static_cast<uint8_t>(
        (frequency_mhz + SX126X_IMAGE_CALIBRATION_STEP_IN_MHZ - 1u) /
        SX126X_IMAGE_CALIBRATION_STEP_IN_MHZ);
    if (frequency_mhz >= 430u && frequency_mhz <= 440u) {
      calibration_lower = 0x6Bu;
      calibration_upper = 0x6Fu;
    } else if (frequency_mhz >= 470u && frequency_mhz <= 510u) {
      calibration_lower = 0x75u;
      calibration_upper = 0x81u;
    } else if (frequency_mhz >= 779u && frequency_mhz <= 787u) {
      calibration_lower = 0xC1u;
      calibration_upper = 0xC5u;
    } else if (frequency_mhz >= 863u && frequency_mhz <= 870u) {
      calibration_lower = 0xD7u;
      calibration_upper = 0xDBu;
    } else if (frequency_mhz >= 902u && frequency_mhz <= 928u) {
      calibration_lower = 0xE1u;
      calibration_upper = 0xE9u;
    }
    status = map_status(
        context, sx126x_cal_img(context, calibration_lower, calibration_upper));
  }
  if (status == HAL_OK) {
    status = map_status(
        context, sx126x_set_rf_freq(context, context->modem.frequency_hz));
  }
  if (status == HAL_OK) {
    status =
        map_status(context, sx126x_set_lora_mod_params(context, &modulation));
  }
  if (status == HAL_OK) {
    const size_t payload_length = context->modem.explicit_header
                                      ? HAL_LORA_RADIO_MAX_PAYLOAD
                                      : context->modem.implicit_payload_length;
    status = configure_packet(context, payload_length);
  }
  if (status == HAL_OK) {
    status = map_status(
        context, sx126x_set_lora_sync_word(context, context->modem.sync_word));
  }
  if (status == HAL_OK) {
    status = map_status(
        context, sx126x_set_tx_params(context, context->modem.tx_power_dbm,
                                      SX126X_RAMP_200_US));
  }
  return status;
}

static hal_status_t sx126x_transmit_start(jh_lora_radio_context_t *context,
                                          uint32_t timeout_ms) {
  hal_status_t status = configure_packet(context, context->tx_length);
  if (status == HAL_OK) {
    status = map_status(
        context, sx126x_write_buffer(context, 0u, context->tx_buffer,
                                     static_cast<uint8_t>(context->tx_length)));
  }
  const sx126x_irq_mask_t irq_mask = SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT;
  if (status == HAL_OK) {
    status =
        map_status(context, sx126x_clear_irq_status(context, SX126X_IRQ_ALL));
  }
  if (status == HAL_OK) {
    status = map_status(
        context, sx126x_set_dio_irq_params(context, irq_mask, irq_mask,
                                           SX126X_IRQ_NONE, SX126X_IRQ_NONE));
  }
  if (status == HAL_OK) {
    jh_sx126x_set_rf_tx(context);
    status = map_status(context, sx126x_set_tx(context, timeout_ms));
  }
  if (status != HAL_OK) {
    jh_sx126x_set_rf_idle(context);
  }
  return status;
}

static hal_status_t sx126x_receive_start(jh_lora_radio_context_t *context,
                                         uint32_t timeout_ms, bool continuous) {
  const size_t payload_length = context->modem.explicit_header
                                    ? HAL_LORA_RADIO_MAX_PAYLOAD
                                    : context->modem.implicit_payload_length;
  hal_status_t status = configure_packet(context, payload_length);
  const sx126x_irq_mask_t irq_mask = SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT |
                                     SX126X_IRQ_CRC_ERROR |
                                     SX126X_IRQ_HEADER_ERROR;
  if (status == HAL_OK) {
    status =
        map_status(context, sx126x_clear_irq_status(context, SX126X_IRQ_ALL));
  }
  if (status == HAL_OK) {
    status = map_status(
        context, sx126x_set_dio_irq_params(context, irq_mask, irq_mask,
                                           SX126X_IRQ_NONE, SX126X_IRQ_NONE));
  }
  if (status == HAL_OK) {
    jh_sx126x_set_rf_rx(context);
    status = continuous
                 ? map_status(context, sx126x_set_rx_with_timeout_in_rtc_step(
                                           context, SX126X_RX_CONTINUOUS))
                 : map_status(context, sx126x_set_rx(context, timeout_ms));
  }
  if (status != HAL_OK) {
    jh_sx126x_set_rf_idle(context);
  }
  return status;
}

static hal_status_t sx126x_cancel(jh_lora_radio_context_t *context) {
  jh_sx126x_set_rf_idle(context);
  hal_status_t status =
      map_status(context, sx126x_set_standby(context, SX126X_STANDBY_CFG_RC));
  if (status == HAL_OK) {
    status = map_status(context, sx126x_stop_rtc(context));
  }
  if (status == HAL_OK) {
    status =
        map_status(context, sx126x_clear_irq_status(context, SX126X_IRQ_ALL));
  }
  if (status == HAL_OK) {
    status = map_status(context, sx126x_set_dio_irq_params(
                                     context, SX126X_IRQ_NONE, SX126X_IRQ_NONE,
                                     SX126X_IRQ_NONE, SX126X_IRQ_NONE));
  }
  return status;
}

static hal_status_t sx126x_process(jh_lora_radio_context_t *context,
                                   jh_lora_provider_events_t *out_events) {
  if (out_events == NULL) {
    return HAL_EINVAL;
  }
  *out_events = JH_LORA_PROVIDER_EVENT_NONE;
  const uint32_t now = hal_millis();
  const bool tx_timeout = context->state == HAL_LORA_RADIO_STATE_TX &&
                          (uint32_t)(now - context->transmit_started_ms) >=
                              context->transmit_timeout_ms;
  const bool rx_timeout = context->state == HAL_LORA_RADIO_STATE_RX &&
                          !context->receive_continuous &&
                          (uint32_t)(now - context->receive_started_ms) >=
                              context->receive_timeout_ms;
  if (!s_dio1_pending &&
      !hal_gpio_read(context->config.hardware.sx126x.dio1_pin) && !tx_timeout &&
      !rx_timeout) {
    return HAL_EAGAIN;
  }
  s_dio1_pending = false;

  sx126x_irq_mask_t irq = SX126X_IRQ_NONE;
  hal_status_t status =
      map_status(context, sx126x_get_irq_status(context, &irq));
  if (status != HAL_OK) {
    return status;
  }
  if (irq != SX126X_IRQ_NONE) {
    *out_events |= JH_LORA_PROVIDER_EVENT_IRQ;
  }
  if ((irq & SX126X_IRQ_CRC_ERROR) != 0u) {
    *out_events |= JH_LORA_PROVIDER_EVENT_CRC_ERROR;
  }
  if ((irq & SX126X_IRQ_HEADER_ERROR) != 0u) {
    *out_events |= JH_LORA_PROVIDER_EVENT_HEADER_ERROR;
  }
  if ((*out_events & (JH_LORA_PROVIDER_EVENT_CRC_ERROR |
                      JH_LORA_PROVIDER_EVENT_HEADER_ERROR)) != 0u) {
    status = map_status(context, sx126x_clear_irq_status(context, irq));
    if (!context->receive_continuous) {
      jh_sx126x_set_rf_idle(context);
    }
    return status;
  }
  if ((irq & SX126X_IRQ_TIMEOUT) != 0u || tx_timeout || rx_timeout) {
    *out_events |= JH_LORA_PROVIDER_EVENT_TIMEOUT;
    status = sx126x_cancel(context);
    return status;
  }
  if ((irq & SX126X_IRQ_TX_DONE) != 0u) {
    *out_events |= JH_LORA_PROVIDER_EVENT_TX_DONE;
    status = map_status(context, sx126x_clear_irq_status(context, irq));
    jh_sx126x_set_rf_idle(context);
    return status;
  }
  if ((irq & SX126X_IRQ_RX_DONE) == 0u) {
    return HAL_EAGAIN;
  }

  sx126x_rx_buffer_status_t buffer_status = {};
  sx126x_pkt_status_lora_t packet_status = {};
  if (!context->receive_continuous) {
    status = map_status(context, sx126x_handle_rx_done(context));
  }
  if (status == HAL_OK) {
    status = map_status(context,
                        sx126x_get_rx_buffer_status(context, &buffer_status));
  }
  if (status == HAL_OK) {
    status = map_status(
        context,
        sx126x_read_buffer(context, buffer_status.buffer_start_pointer,
                           context->rx_buffer, buffer_status.pld_len_in_bytes));
  }
  if (status == HAL_OK) {
    status = map_status(context,
                        sx126x_get_lora_pkt_status(context, &packet_status));
  }
  (void)sx126x_clear_irq_status(context, irq);
  if (status != HAL_OK) {
    jh_sx126x_set_rf_idle(context);
    return status;
  }
  context->rx_length = buffer_status.pld_len_in_bytes;
  context->rx_info.rssi_dbm =
      signed_byte_to_int16(packet_status.rssi_pkt_in_dbm);
  context->rx_info.snr_db = packet_status.snr_pkt_in_db;
  context->rx_info.signal_rssi_dbm =
      signed_byte_to_int16(packet_status.signal_rssi_pkt_in_dbm);
  context->rx_info.timestamp_ms = hal_millis();
  context->rx_info.crc_valid = true;
  context->rx_ready = true;
  *out_events |= JH_LORA_PROVIDER_EVENT_RX_DONE;
  if (!context->receive_continuous) {
    jh_sx126x_set_rf_idle(context);
  }
  return HAL_OK;
}

static hal_status_t sx126x_sleep(jh_lora_radio_context_t *context) {
  jh_sx126x_set_rf_idle(context);
  return map_status(context,
                    sx126x_set_sleep(context, SX126X_SLEEP_CFG_WARM_START));
}

static hal_status_t sx126x_standby(jh_lora_radio_context_t *context) {
  jh_sx126x_set_rf_idle(context);
  return map_status(context,
                    sx126x_set_standby(context, SX126X_STANDBY_CFG_RC));
}

[[maybe_unused]] static const jh_lora_radio_provider_ops_t s_sx126x_provider = {
    sx126x_initialize,     sx126x_deinitialize,  sx126x_configure,
    sx126x_transmit_start, sx126x_receive_start, sx126x_process,
    sx126x_cancel,         sx126x_sleep,         sx126x_standby,
};

const jh_lora_radio_provider_ops_t *jh_sx126x_provider_ops(void) {
  return &s_sx126x_provider;
}

#if !HAL_TARGET_IS_MOCK
const jh_lora_radio_provider_ops_t *jh_lora_radio_default_provider(void) {
  return jh_sx126x_provider_ops();
}
#endif

#endif /* HAL_ENABLE_SX126X */
