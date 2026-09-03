#include "jh_sx127x_adapter.h"

#ifdef HAL_ENABLE_SX127X

#include "hal/core/jh_endian.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/radio/jh_lora_modem.h"
#include "hal/spi/hal_spi.h"
#include "hal/system/hal_system.h"

#include <string.h>

#define JH_SX127X_REG_FIFO 0x00u
#define JH_SX127X_REG_OP_MODE 0x01u
#define JH_SX127X_REG_FRF_MSB 0x06u
#define JH_SX127X_REG_PA_CONFIG 0x09u
#define JH_SX127X_REG_LNA 0x0Cu
#define JH_SX127X_REG_FIFO_ADDR_PTR 0x0Du
#define JH_SX127X_REG_FIFO_TX_BASE 0x0Eu
#define JH_SX127X_REG_FIFO_RX_BASE 0x0Fu
#define JH_SX127X_REG_FIFO_RX_CURRENT 0x10u
#define JH_SX127X_REG_IRQ_FLAGS_MASK 0x11u
#define JH_SX127X_REG_IRQ_FLAGS 0x12u
#define JH_SX127X_REG_RX_BYTES 0x13u
#define JH_SX127X_REG_PACKET_SNR 0x19u
#define JH_SX127X_REG_PACKET_RSSI 0x1Au
#define JH_SX127X_REG_RSSI 0x1Bu
#define JH_SX127X_REG_MODEM_CONFIG_1 0x1Du
#define JH_SX127X_REG_MODEM_CONFIG_2 0x1Eu
#define JH_SX127X_REG_PREAMBLE_MSB 0x20u
#define JH_SX127X_REG_PAYLOAD_LENGTH 0x22u
#define JH_SX127X_REG_MODEM_CONFIG_3 0x26u
#define JH_SX127X_REG_DETECTION_OPTIMIZE 0x31u
#define JH_SX127X_REG_INVERT_IQ 0x33u
#define JH_SX127X_REG_DETECTION_THRESHOLD 0x37u
#define JH_SX127X_REG_SYNC_WORD 0x39u
#define JH_SX127X_REG_INVERT_IQ_2 0x3Bu
#define JH_SX127X_REG_DIO_MAPPING_1 0x40u
#define JH_SX127X_REG_VERSION 0x42u
#define JH_SX127X_REG_PA_DAC 0x4Du

#define JH_SX127X_LONG_RANGE_MODE 0x80u
#define JH_SX127X_LOW_FREQUENCY_MODE 0x08u
#define JH_SX127X_MODE_SLEEP 0x00u
#define JH_SX127X_MODE_STANDBY 0x01u
#define JH_SX127X_MODE_TX 0x03u
#define JH_SX127X_MODE_RX_CONTINUOUS 0x05u
#define JH_SX127X_MODE_RX_SINGLE 0x06u
#define JH_SX127X_MODE_CAD 0x07u

#define JH_SX127X_IRQ_RX_TIMEOUT 0x80u
#define JH_SX127X_IRQ_RX_DONE 0x40u
#define JH_SX127X_IRQ_CRC_ERROR 0x20u
#define JH_SX127X_IRQ_HEADER_VALID 0x10u
#define JH_SX127X_IRQ_TX_DONE 0x08u
#define JH_SX127X_IRQ_CAD_DONE 0x04u
#define JH_SX127X_IRQ_CAD_DETECTED 0x01u
#define JH_SX127X_IRQ_ALL 0xFFu

#define JH_SX127X_VERSION 0x12u
#define JH_SX127X_RESET_ASSERT_US UINT32_C(200)

static volatile bool s_irq_pending = false;

static void sx127x_interrupt(void) { s_irq_pending = true; }

static uint32_t spi_clock(const jh_lora_radio_context_t *context) {
  return context->config.spi_clock_hz == 0u ? HAL_LORA_SPI_CLOCK_DEFAULT_HZ
                                            : context->config.spi_clock_hz;
}

static hal_status_t transaction_begin(jh_lora_radio_context_t *context) {
  const hal_spi_settings_t settings = {spi_clock(context), HAL_SPI_MSBFIRST,
                                       HAL_SPI_MODE0};
  hal_spi_lock(context->config.spi_bus);
  const hal_status_t status =
      hal_spi_begin_transaction(context->config.spi_bus, &settings);
  if (status != HAL_OK) {
    hal_spi_unlock(context->config.spi_bus);
    context->provider_last_status = HAL_EBUS;
    return HAL_EBUS;
  }
  hal_gpio_write(context->config.cs_pin, false);
  return HAL_OK;
}

static hal_status_t transaction_end(jh_lora_radio_context_t *context,
                                    hal_status_t status) {
  hal_gpio_write(context->config.cs_pin, true);
  const hal_status_t end_status =
      hal_spi_end_transaction(context->config.spi_bus);
  hal_spi_unlock(context->config.spi_bus);
  if (status == HAL_OK && end_status != HAL_OK) {
    status = HAL_EBUS;
  }
  context->provider_last_status = status;
  return status;
}

static hal_status_t write_buffer(jh_lora_radio_context_t *context,
                                 uint8_t address, const uint8_t *data,
                                 size_t length) {
  if (data == NULL || length == 0u) {
    return HAL_EINVAL;
  }
  hal_status_t status = transaction_begin(context);
  const uint8_t command = address | 0x80u;
  if (status == HAL_OK) {
    status = hal_spi_write(context->config.spi_bus, &command, 1u);
  }
  if (status == HAL_OK) {
    status = hal_spi_write(context->config.spi_bus, data, length);
  }
  return transaction_end(context, status == HAL_OK ? HAL_OK : HAL_EBUS);
}

static hal_status_t read_buffer(jh_lora_radio_context_t *context,
                                uint8_t address, uint8_t *data, size_t length) {
  if (data == NULL || length == 0u) {
    return HAL_EINVAL;
  }
  hal_status_t status = transaction_begin(context);
  const uint8_t command = address & 0x7Fu;
  if (status == HAL_OK) {
    status = hal_spi_write(context->config.spi_bus, &command, 1u);
  }
  for (size_t index = 0u; status == HAL_OK && index < length; ++index) {
    status = hal_spi_transfer_ex(context->config.spi_bus, 0u, &data[index]);
  }
  return transaction_end(context, status == HAL_OK ? HAL_OK : HAL_EBUS);
}

extern "C" hal_status_t
jh_sx127x_write_register(jh_lora_radio_context_t *context, uint8_t address,
                         uint8_t value) {
  if (context == NULL || address >= 0x80u) {
    return HAL_EINVAL;
  }
  return write_buffer(context, address, &value, 1u);
}

extern "C" hal_status_t
jh_sx127x_read_register(jh_lora_radio_context_t *context, uint8_t address,
                        uint8_t *out_value) {
  if (context == NULL || address >= 0x80u || out_value == NULL) {
    return HAL_EINVAL;
  }
  return read_buffer(context, address, out_value, 1u);
}

static void set_rf_path(jh_lora_radio_context_t *context, bool receive,
                        bool transmit) {
  const hal_lora_sx127x_hardware_config_t *hardware =
      &context->config.hardware.sx127x;
  if (hardware->rf_switch_rx_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_write(hardware->rf_switch_rx_pin,
                   receive ? hardware->rf_switch_rx_active_level
                           : !hardware->rf_switch_rx_active_level);
  }
  if (hardware->rf_switch_tx_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_write(hardware->rf_switch_tx_pin,
                   transmit ? hardware->rf_switch_tx_active_level
                            : !hardware->rf_switch_tx_active_level);
  }
}

static uint8_t op_mode(const jh_lora_radio_context_t *context, uint8_t mode) {
  const uint32_t frequency_hz =
      context->configured ? context->modem.frequency_hz
                          : context->config.hardware.sx127x.min_frequency_hz;
  return JH_SX127X_LONG_RANGE_MODE |
         (frequency_hz < UINT32_C(525000000) ? JH_SX127X_LOW_FREQUENCY_MODE
                                             : 0u) |
         mode;
}

static hal_status_t set_mode(jh_lora_radio_context_t *context, uint8_t mode) {
  return jh_sx127x_write_register(context, JH_SX127X_REG_OP_MODE,
                                  op_mode(context, mode));
}

static bool bandwidth_register(uint32_t bandwidth_hz, uint8_t *out_value) {
  if (out_value == NULL) {
    return false;
  }
  switch (bandwidth_hz) {
  case 7800u:
  case 7812u:
    *out_value = 0u;
    return true;
  case 10400u:
  case 10417u:
    *out_value = 1u;
    return true;
  case 15600u:
  case 15625u:
    *out_value = 2u;
    return true;
  case 20800u:
  case 20833u:
    *out_value = 3u;
    return true;
  case 31250u:
    *out_value = 4u;
    return true;
  case 41667u:
  case 41700u:
    *out_value = 5u;
    return true;
  case 62500u:
    *out_value = 6u;
    return true;
  case 125000u:
    *out_value = 7u;
    return true;
  case 250000u:
    *out_value = 8u;
    return true;
  case 500000u:
    *out_value = 9u;
    return true;
  default:
    return false;
  }
}

static hal_status_t set_frequency(jh_lora_radio_context_t *context,
                                  uint32_t frequency_hz) {
  const uint64_t frf =
      ((uint64_t)frequency_hz * (UINT64_C(1) << 19u) + UINT64_C(16000000)) /
      UINT64_C(32000000);
  const uint8_t bytes[] = {static_cast<uint8_t>(frf >> 16u),
                           static_cast<uint8_t>(frf >> 8u),
                           static_cast<uint8_t>(frf)};
  return write_buffer(context, JH_SX127X_REG_FRF_MSB, bytes, sizeof(bytes));
}

static hal_status_t set_tx_power(jh_lora_radio_context_t *context,
                                 int8_t power_dbm) {
  const hal_lora_sx127x_pa_output_t output =
      context->config.hardware.sx127x.pa_output;
  uint8_t pa_config = 0u;
  uint8_t pa_dac = 0x84u;
  if (output == HAL_LORA_SX127X_PA_BOOST) {
    if (power_dbm > 17) {
      pa_dac = 0x87u;
      pa_config = 0x80u | static_cast<uint8_t>(power_dbm - 5);
    } else {
      pa_config = 0x80u | static_cast<uint8_t>(power_dbm - 2);
    }
  } else if (power_dbm > 0) {
    pa_config = 0x70u | static_cast<uint8_t>(power_dbm);
  } else {
    pa_config = static_cast<uint8_t>(power_dbm + 4);
  }
  hal_status_t status =
      jh_sx127x_write_register(context, JH_SX127X_REG_PA_CONFIG, pa_config);
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_PA_DAC, pa_dac);
  }
  return status;
}

static hal_status_t sx127x_initialize(jh_lora_radio_context_t *context) {
  const hal_lora_sx127x_hardware_config_t *hardware =
      &context->config.hardware.sx127x;
  hal_status_t status =
      hal_spi_init(context->config.spi_bus, context->config.spi_miso_pin,
                   context->config.spi_mosi_pin, context->config.spi_sck_pin);
  if (status != HAL_OK) {
    context->provider_last_status = HAL_EBUS;
    return HAL_EBUS;
  }
  hal_gpio_set_mode(context->config.cs_pin, HAL_GPIO_OUTPUT_HIGH);
  hal_gpio_set_mode(hardware->reset_pin, HAL_GPIO_OUTPUT_HIGH);
  hal_gpio_set_mode(hardware->dio0_pin, HAL_GPIO_INPUT);
  if (hardware->dio1_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_set_mode(hardware->dio1_pin, HAL_GPIO_INPUT);
  }
  if (hardware->dio2_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_set_mode(hardware->dio2_pin, HAL_GPIO_INPUT);
  }
  if (hardware->rf_switch_rx_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_set_mode(hardware->rf_switch_rx_pin,
                      hardware->rf_switch_rx_active_level
                          ? HAL_GPIO_OUTPUT_LOW
                          : HAL_GPIO_OUTPUT_HIGH);
  }
  if (hardware->rf_switch_tx_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_set_mode(hardware->rf_switch_tx_pin,
                      hardware->rf_switch_tx_active_level
                          ? HAL_GPIO_OUTPUT_LOW
                          : HAL_GPIO_OUTPUT_HIGH);
  }
  if (hardware->tcxo_enable_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_set_mode(hardware->tcxo_enable_pin, hardware->tcxo_active_level
                                                     ? HAL_GPIO_OUTPUT_HIGH
                                                     : HAL_GPIO_OUTPUT_LOW);
    hal_delay_us(hardware->tcxo_startup_us);
  }
  set_rf_path(context, false, false);
  hal_gpio_write(hardware->reset_pin, false);
  hal_delay_us(JH_SX127X_RESET_ASSERT_US);
  hal_gpio_write(hardware->reset_pin, true);
  hal_delay_ms(HAL_LORA_SX127X_RESET_SETTLE_MS);

  uint8_t version = 0u;
  status = jh_sx127x_read_register(context, JH_SX127X_REG_VERSION, &version);
  if (status != HAL_OK || version != JH_SX127X_VERSION) {
    return status == HAL_OK ? HAL_EHW : status;
  }
  status = set_mode(context, JH_SX127X_MODE_SLEEP);
  if (status == HAL_OK) {
    status = set_mode(context, JH_SX127X_MODE_STANDBY);
  }
  if (status == HAL_OK) {
    const uint8_t fifo_bases[] = {0u, 0u};
    status = write_buffer(context, JH_SX127X_REG_FIFO_TX_BASE, fifo_bases,
                          sizeof(fifo_bases));
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_LNA, 0x23u);
  }
  if (status == HAL_OK) {
    status =
        jh_sx127x_write_register(context, JH_SX127X_REG_IRQ_FLAGS_MASK, 0u);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_IRQ_FLAGS,
                                      JH_SX127X_IRQ_ALL);
  }
  if (status == HAL_OK) {
    hal_gpio_attach_interrupt(hardware->dio0_pin, sx127x_interrupt,
                              HAL_GPIO_IRQ_RISING);
    if (hardware->dio1_pin != HAL_LORA_PIN_NONE) {
      hal_gpio_attach_interrupt(hardware->dio1_pin, sx127x_interrupt,
                                HAL_GPIO_IRQ_RISING);
    }
    if (hardware->dio2_pin != HAL_LORA_PIN_NONE) {
      hal_gpio_attach_interrupt(hardware->dio2_pin, sx127x_interrupt,
                                HAL_GPIO_IRQ_RISING);
    }
    context->provider_irq_attached = true;
  }
  return status;
}

static hal_status_t sx127x_deinitialize(jh_lora_radio_context_t *context) {
  const hal_lora_sx127x_hardware_config_t *hardware =
      &context->config.hardware.sx127x;
  if (context->provider_irq_attached) {
    hal_gpio_detach_interrupt(hardware->dio0_pin);
    if (hardware->dio1_pin != HAL_LORA_PIN_NONE) {
      hal_gpio_detach_interrupt(hardware->dio1_pin);
    }
    if (hardware->dio2_pin != HAL_LORA_PIN_NONE) {
      hal_gpio_detach_interrupt(hardware->dio2_pin);
    }
    context->provider_irq_attached = false;
  }
  set_rf_path(context, false, false);
  const hal_status_t status = set_mode(context, JH_SX127X_MODE_SLEEP);
  if (hardware->tcxo_enable_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_write(hardware->tcxo_enable_pin, !hardware->tcxo_active_level);
  }
  return status;
}

static hal_status_t sx127x_configure(jh_lora_radio_context_t *context) {
  uint8_t bandwidth = 0u;
  if (!bandwidth_register(context->modem.bandwidth_hz, &bandwidth)) {
    return HAL_EINVAL;
  }
  hal_status_t status = set_mode(context, JH_SX127X_MODE_STANDBY);
  if (status == HAL_OK) {
    status = set_frequency(context, context->modem.frequency_hz);
  }
  const uint8_t modem_config_1 = static_cast<uint8_t>(
      (bandwidth << 4u) | ((context->modem.coding_rate - 4u) << 1u) |
      (context->modem.explicit_header ? 0u : 1u));
  const uint8_t modem_config_2 =
      static_cast<uint8_t>((context->modem.spreading_factor << 4u) |
                           (context->modem.crc_enabled ? 0x04u : 0u));
  const uint64_t symbol_us =
      ((UINT64_C(1) << context->modem.spreading_factor) * UINT64_C(1000000) +
       context->modem.bandwidth_hz - 1u) /
      context->modem.bandwidth_hz;
  const uint8_t modem_config_3 = symbol_us >= UINT64_C(16000) ? 0x0Cu : 0x04u;
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_MODEM_CONFIG_1,
                                      modem_config_1);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_MODEM_CONFIG_2,
                                      modem_config_2);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_MODEM_CONFIG_3,
                                      modem_config_3);
  }
  uint8_t preamble[2];
  jh_store_be16(preamble, context->modem.preamble_symbols);
  if (status == HAL_OK) {
    status = write_buffer(context, JH_SX127X_REG_PREAMBLE_MSB, preamble,
                          sizeof(preamble));
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_SYNC_WORD,
                                      context->modem.sync_word);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_INVERT_IQ,
                                      context->modem.invert_iq ? 0x66u : 0x27u);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_INVERT_IQ_2,
                                      context->modem.invert_iq ? 0x19u : 0x1Du);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(
        context, JH_SX127X_REG_DETECTION_OPTIMIZE,
        context->modem.spreading_factor == 6u ? 0x05u : 0x03u);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(
        context, JH_SX127X_REG_DETECTION_THRESHOLD,
        context->modem.spreading_factor == 6u ? 0x0Cu : 0x0Au);
  }
  if (status == HAL_OK) {
    status = set_tx_power(context, context->modem.tx_power_dbm);
  }
  return status;
}

static hal_status_t
sx127x_get_capabilities(jh_lora_radio_context_t *context,
                        hal_lora_radio_capabilities_t *out_capabilities) {
  return jh_lora_radio_describe_capabilities(
      context, JH_LORA_PROVIDER_CAP_SX127X, out_capabilities);
}

static int16_t rssi_offset(const jh_lora_radio_context_t *context) {
  return context->modem.frequency_hz < UINT32_C(779000000) ? -164 : -157;
}

static hal_status_t sx127x_get_instant_rssi(jh_lora_radio_context_t *context,
                                            int16_t *out_rssi_dbm) {
  if (out_rssi_dbm == NULL) {
    return HAL_EINVAL;
  }
  uint8_t raw = 0u;
  const hal_status_t status =
      jh_sx127x_read_register(context, JH_SX127X_REG_RSSI, &raw);
  if (status == HAL_OK) {
    *out_rssi_dbm = static_cast<int16_t>(rssi_offset(context) + raw);
  }
  return status;
}

static hal_status_t sx127x_transmit_start(jh_lora_radio_context_t *context,
                                          uint32_t timeout_ms) {
  (void)timeout_ms;
  if (!context->modem.explicit_header &&
      context->tx_length != context->modem.implicit_payload_length) {
    return HAL_EINVAL;
  }
  hal_status_t status = set_mode(context, JH_SX127X_MODE_STANDBY);
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_FIFO_ADDR_PTR, 0u);
  }
  if (status == HAL_OK) {
    status = write_buffer(context, JH_SX127X_REG_FIFO, context->tx_buffer,
                          context->tx_length);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_PAYLOAD_LENGTH,
                                      static_cast<uint8_t>(context->tx_length));
  }
  if (status == HAL_OK) {
    status =
        jh_sx127x_write_register(context, JH_SX127X_REG_DIO_MAPPING_1, 0x40u);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_IRQ_FLAGS,
                                      JH_SX127X_IRQ_ALL);
  }
  if (status == HAL_OK) {
    set_rf_path(context, false, true);
    status = set_mode(context, JH_SX127X_MODE_TX);
  }
  if (status != HAL_OK) {
    set_rf_path(context, false, false);
  }
  return status;
}

static hal_status_t sx127x_receive_start(jh_lora_radio_context_t *context,
                                         uint32_t timeout_ms, bool continuous) {
  (void)timeout_ms;
  const uint8_t payload_length =
      context->modem.explicit_header
          ? static_cast<uint8_t>(HAL_LORA_RADIO_MAX_PAYLOAD)
          : context->modem.implicit_payload_length;
  hal_status_t status = jh_sx127x_write_register(
      context, JH_SX127X_REG_PAYLOAD_LENGTH, payload_length);
  if (status == HAL_OK) {
    status =
        jh_sx127x_write_register(context, JH_SX127X_REG_DIO_MAPPING_1, 0x00u);
  }
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_IRQ_FLAGS,
                                      JH_SX127X_IRQ_ALL);
  }
  if (status == HAL_OK) {
    set_rf_path(context, true, false);
    status = set_mode(context, continuous ? JH_SX127X_MODE_RX_CONTINUOUS
                                          : JH_SX127X_MODE_RX_SINGLE);
  }
  if (status != HAL_OK) {
    set_rf_path(context, false, false);
  }
  return status;
}

static hal_status_t
sx127x_channel_activity_detect_start(jh_lora_radio_context_t *context,
                                     uint32_t timeout_ms) {
  (void)timeout_ms;
  hal_status_t status =
      jh_sx127x_write_register(context, JH_SX127X_REG_DIO_MAPPING_1, 0xA0u);
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_IRQ_FLAGS,
                                      JH_SX127X_IRQ_ALL);
  }
  if (status == HAL_OK) {
    set_rf_path(context, true, false);
    status = set_mode(context, JH_SX127X_MODE_CAD);
  }
  if (status != HAL_OK) {
    set_rf_path(context, false, false);
  }
  return status;
}

static bool operation_timed_out(const jh_lora_radio_context_t *context) {
  if (context->state == HAL_LORA_RADIO_STATE_TX) {
    return hal_millis_deadline_expired(context->transmit_started_ms,
                                       context->transmit_timeout_ms);
  }
  if (context->state == HAL_LORA_RADIO_STATE_RX &&
      !context->receive_continuous) {
    return hal_millis_deadline_expired(context->receive_started_ms,
                                       context->receive_timeout_ms);
  }
  return context->state == HAL_LORA_RADIO_STATE_CAD &&
         hal_millis_deadline_expired(context->channel_activity_started_ms,
                                     context->channel_activity_timeout_ms);
}

static hal_status_t read_received_packet(jh_lora_radio_context_t *context) {
  uint8_t length = 0u;
  uint8_t address = 0u;
  hal_status_t status =
      jh_sx127x_read_register(context, JH_SX127X_REG_RX_BYTES, &length);
  if (status == HAL_OK) {
    status = jh_sx127x_read_register(context, JH_SX127X_REG_FIFO_RX_CURRENT,
                                     &address);
  }
  if (status == HAL_OK) {
    status =
        jh_sx127x_write_register(context, JH_SX127X_REG_FIFO_ADDR_PTR, address);
  }
  if (status == HAL_OK && length > 0u) {
    status =
        read_buffer(context, JH_SX127X_REG_FIFO, context->rx_buffer, length);
  }
  uint8_t raw_snr = 0u;
  uint8_t raw_rssi = 0u;
  if (status == HAL_OK) {
    status =
        jh_sx127x_read_register(context, JH_SX127X_REG_PACKET_SNR, &raw_snr);
  }
  if (status == HAL_OK) {
    status =
        jh_sx127x_read_register(context, JH_SX127X_REG_PACKET_RSSI, &raw_rssi);
  }
  if (status == HAL_OK) {
    context->rx_length = length;
    context->rx_info.snr_db = static_cast<int8_t>(raw_snr) / 4;
    context->rx_info.rssi_dbm =
        static_cast<int16_t>(rssi_offset(context) + raw_rssi);
    context->rx_info.signal_rssi_dbm = context->rx_info.rssi_dbm;
    context->rx_info.timestamp_ms = hal_millis();
    context->rx_info.crc_valid = true;
    context->rx_ready = true;
  }
  return status;
}

static hal_status_t sx127x_process(jh_lora_radio_context_t *context,
                                   jh_lora_provider_events_t *out_events) {
  if (out_events == NULL) {
    return HAL_EINVAL;
  }
  *out_events = JH_LORA_PROVIDER_EVENT_NONE;
  if (operation_timed_out(context)) {
    set_rf_path(context, false, false);
    const hal_status_t status = set_mode(context, JH_SX127X_MODE_STANDBY);
    if (status == HAL_OK) {
      *out_events = JH_LORA_PROVIDER_EVENT_TIMEOUT;
    }
    return status;
  }

  uint8_t irq = 0u;
  hal_status_t status =
      jh_sx127x_read_register(context, JH_SX127X_REG_IRQ_FLAGS, &irq);
  s_irq_pending = false;
  if (status != HAL_OK) {
    return status;
  }
  if (irq == 0u) {
    return HAL_EAGAIN;
  }
  status = jh_sx127x_write_register(context, JH_SX127X_REG_IRQ_FLAGS, irq);
  if (status != HAL_OK) {
    return status;
  }
  *out_events |= JH_LORA_PROVIDER_EVENT_IRQ;
  if ((irq & JH_SX127X_IRQ_CRC_ERROR) != 0u) {
    *out_events |= JH_LORA_PROVIDER_EVENT_CRC_ERROR;
  }
  if ((irq & JH_SX127X_IRQ_RX_TIMEOUT) != 0u) {
    *out_events |= JH_LORA_PROVIDER_EVENT_TIMEOUT;
  }
  if ((irq & JH_SX127X_IRQ_TX_DONE) != 0u) {
    *out_events |= JH_LORA_PROVIDER_EVENT_TX_DONE;
  }
  if ((irq & JH_SX127X_IRQ_CAD_DONE) != 0u) {
    *out_events |= JH_LORA_PROVIDER_EVENT_CAD_DONE;
    if ((irq & JH_SX127X_IRQ_CAD_DETECTED) != 0u) {
      *out_events |= JH_LORA_PROVIDER_EVENT_CAD_DETECTED;
    }
  }
  if ((irq & JH_SX127X_IRQ_RX_DONE) != 0u &&
      (irq & JH_SX127X_IRQ_CRC_ERROR) == 0u) {
    status = read_received_packet(context);
    if (status == HAL_OK) {
      *out_events |= JH_LORA_PROVIDER_EVENT_RX_DONE;
    }
  }
  if ((irq & (JH_SX127X_IRQ_TX_DONE | JH_SX127X_IRQ_RX_TIMEOUT |
              JH_SX127X_IRQ_CAD_DONE)) != 0u ||
      ((irq & JH_SX127X_IRQ_RX_DONE) != 0u && !context->receive_continuous)) {
    set_rf_path(context, false, false);
  }
  return status;
}

static hal_status_t sx127x_cancel(jh_lora_radio_context_t *context) {
  set_rf_path(context, false, false);
  hal_status_t status = set_mode(context, JH_SX127X_MODE_STANDBY);
  if (status == HAL_OK) {
    status = jh_sx127x_write_register(context, JH_SX127X_REG_IRQ_FLAGS,
                                      JH_SX127X_IRQ_ALL);
  }
  return status;
}

static hal_status_t sx127x_sleep(jh_lora_radio_context_t *context) {
  const hal_lora_sx127x_hardware_config_t *hardware =
      &context->config.hardware.sx127x;
  set_rf_path(context, false, false);
  const hal_status_t status = set_mode(context, JH_SX127X_MODE_SLEEP);
  if (status == HAL_OK) {
    if (hardware->tcxo_enable_pin != HAL_LORA_PIN_NONE) {
      hal_gpio_write(hardware->tcxo_enable_pin, !hardware->tcxo_active_level);
    }
    context->provider_sleeping = true;
  }
  return status;
}

static hal_status_t sx127x_standby(jh_lora_radio_context_t *context) {
  const hal_lora_sx127x_hardware_config_t *hardware =
      &context->config.hardware.sx127x;
  if (context->provider_sleeping &&
      hardware->tcxo_enable_pin != HAL_LORA_PIN_NONE) {
    hal_gpio_write(hardware->tcxo_enable_pin, hardware->tcxo_active_level);
    hal_delay_us(hardware->tcxo_startup_us);
  }
  const hal_status_t status = set_mode(context, JH_SX127X_MODE_STANDBY);
  if (status == HAL_OK) {
    context->provider_sleeping = false;
  }
  return status;
}

static hal_status_t sx127x_calibrate(jh_lora_radio_context_t *context) {
  (void)context;
  return HAL_EUNSUPPORTED;
}

static const jh_lora_radio_provider_ops_t s_sx127x_provider = {
    sx127x_initialize,
    sx127x_deinitialize,
    sx127x_configure,
    sx127x_get_capabilities,
    sx127x_get_instant_rssi,
    sx127x_transmit_start,
    sx127x_receive_start,
    sx127x_channel_activity_detect_start,
    sx127x_process,
    sx127x_cancel,
    sx127x_sleep,
    sx127x_standby,
    sx127x_calibrate,
};

const jh_lora_radio_provider_ops_t *jh_sx127x_provider_ops(void) {
  return &s_sx127x_provider;
}

#if !HAL_TARGET_IS_MOCK
const jh_lora_radio_provider_ops_t *jh_lora_radio_default_provider(void) {
  return jh_sx127x_provider_ops();
}
#endif

#endif /* HAL_ENABLE_SX127X */
