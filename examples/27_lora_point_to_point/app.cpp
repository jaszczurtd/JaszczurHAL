/**
 * @file app.cpp
 * @brief Raw LoRa ping/pong example for integrated LF and external HF radios.
 */

#include <hal/hal_app.h>
#include <hal/hal_lora_radio.h>
#include <hal/hal_spi.h>
#include <hal/hal_status.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <tools.h>

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t kLfTestFrequencyHz = UINT32_C(434000000);
#ifndef HAL_LORA_EXAMPLE_RESPONDER
constexpr uint32_t kReplyTimeoutMs = UINT32_C(1500);
constexpr uint32_t kTransmitPeriodMs = UINT32_C(3000);
#endif

hal_lora_radio_t s_radio = nullptr;
hal_lora_radio_config_t s_hardware{};
hal_lora_modem_config_t s_modem{};
bool s_ready = false;
#ifndef HAL_LORA_EXAMPLE_RESPONDER
bool s_waiting_for_reply = false;
uint32_t s_next_transmit_ms = 0u;
uint32_t s_sequence = 0u;
#else
uint32_t s_last_received_sequence = 0u;
#endif

hal_status_t external_core1262_hf_config(hal_lora_radio_config_t *out_config) {
  if (out_config == nullptr) {
    return HAL_EINVAL;
  }
  hal_lora_radio_config_t &config = *out_config;
  config = {};
  config.model = HAL_LORA_RADIO_SX1262;
  config.spi_bus = 0u;
  config.spi_clock_hz = HAL_LORA_SPI_CLOCK_DEFAULT_HZ;
#if HAL_TARGET_IS_RP
  config.spi_miso_pin = 16u;
  config.spi_mosi_pin = 19u;
  config.spi_sck_pin = 18u;
  config.cs_pin = 17u;
  config.hardware.sx126x.reset_pin = 20u;
  config.hardware.sx126x.busy_pin = 21u;
  config.hardware.sx126x.dio1_pin = 22u;
  config.hardware.sx126x.rf_switch_pin_a = 10u; /* RXEN */
  config.hardware.sx126x.rf_switch_pin_b = 11u; /* TXEN */
#else
  /* STM32 pin id = port * 16 + pin: SPI1 PA6/PA7/PA5 and PB0..PB5. */
  config.spi_miso_pin = 6u;
  config.spi_mosi_pin = 7u;
  config.spi_sck_pin = 5u;
  config.cs_pin = 16u;
  config.hardware.sx126x.reset_pin = 17u;
  config.hardware.sx126x.busy_pin = 18u;
  config.hardware.sx126x.dio1_pin = 19u;
  config.hardware.sx126x.rf_switch_pin_a = 20u; /* PB4: RXEN */
  config.hardware.sx126x.rf_switch_pin_b = 21u; /* PB5: TXEN */
#endif
  hal_lora_sx126x_hardware_config_t pins = config.hardware.sx126x;
  const hal_status_t status =
      hal_lora_sx126x_core1262_hf_defaults(&config.hardware.sx126x);
  if (status != HAL_OK) {
    return status;
  }
  config.hardware.sx126x.reset_pin = pins.reset_pin;
  config.hardware.sx126x.busy_pin = pins.busy_pin;
  config.hardware.sx126x.dio1_pin = pins.dio1_pin;
  config.hardware.sx126x.rf_switch_pin_a = pins.rf_switch_pin_a;
  config.hardware.sx126x.rf_switch_pin_b = pins.rf_switch_pin_b;
  return HAL_OK;
}

hal_lora_modem_config_t modem_config(const hal_lora_radio_config_t &hardware) {
  hal_lora_modem_config_t modem = hal_lora_default_eu868();
  if (hardware.hardware.sx126x.max_frequency_hz < UINT32_C(800000000)) {
    /* Deliberate LF hardware-test configuration, not a regulatory preset. */
    modem.frequency_hz = kLfTestFrequencyHz;
    modem.tx_power_dbm = 10;
  }
#ifdef HAL_LORA_EXAMPLE_SF
  modem.spreading_factor = HAL_LORA_EXAMPLE_SF;
#endif
#ifdef HAL_LORA_EXAMPLE_TX_POWER_DBM
  modem.tx_power_dbm = HAL_LORA_EXAMPLE_TX_POWER_DBM;
#endif
  return modem;
}

void log_packet(const char *direction, const uint8_t *data, size_t length,
                const hal_lora_packet_info_t &info) {
  char text[HAL_LORA_RADIO_MAX_PAYLOAD + 1u]{};
  const size_t copied =
      length < HAL_LORA_RADIO_MAX_PAYLOAD ? length : HAL_LORA_RADIO_MAX_PAYLOAD;
  memcpy(text, data, copied);
  deb("%s '%s' RSSI=%d dBm SNR=%d dB", direction, text, (int)info.rssi_dbm,
      (int)info.snr_db);
}

#ifdef HAL_LORA_EXAMPLE_RESPONDER
void report_sequence_loss(uint32_t sequence) {
  if (s_last_received_sequence != 0u &&
      sequence > s_last_received_sequence + 1u) {
    deb("Lost %lu packet(s)",
        (unsigned long)(sequence - s_last_received_sequence - 1u));
  }
  s_last_received_sequence = sequence;
}

void start_responder_receive(void) {
  const hal_status_t status = hal_lora_radio_receive_start_continuous(s_radio);
  if (status != HAL_OK) {
    derr("RX start failed: %s", hal_status_to_string(status));
  }
}

void responder_poll(void) {
  uint8_t packet[HAL_LORA_RADIO_MAX_PAYLOAD]{};
  size_t length = 0u;
  hal_lora_packet_info_t info{};
  const hal_status_t status =
      hal_lora_radio_receive(s_radio, packet, sizeof(packet), &length, &info);
  if (status == HAL_EAGAIN) {
    return;
  }
  if (status != HAL_OK) {
    derr("RX failed: %s", hal_status_to_string(status));
    (void)hal_lora_radio_standby(s_radio);
    start_responder_receive();
    return;
  }
  log_packet("RX", packet, length, info);

  char received[HAL_LORA_RADIO_MAX_PAYLOAD + 1u]{};
  memcpy(received, packet, length);
  unsigned long sequence = 0u;
  unsigned long sent_ms = 0u;
  if (sscanf(received, "JHLORA1 PING %lu %lu", &sequence, &sent_ms) != 2) {
    deb("Ignoring packet outside the example protocol");
    return;
  }
  (void)sent_ms;
  report_sequence_loss((uint32_t)sequence);
  char reply[64];
  const int written = snprintf(reply, sizeof(reply), "JHLORA1 PONG %lu %lu",
                               sequence, (unsigned long)hal_millis());
  (void)hal_lora_radio_standby(s_radio);
  if ((sequence % 10u) == 0u) {
    const hal_status_t sleep = hal_lora_radio_sleep(s_radio);
    hal_delay_ms(100u);
    const hal_status_t wake = hal_lora_radio_standby(s_radio);
    deb("Sleep/wake sequence=%lu sleep=%s wake=%s", sequence,
        hal_status_to_string(sleep), hal_status_to_string(wake));
  }
  if (written > 0 && (size_t)written < sizeof(reply)) {
    const hal_status_t tx = hal_lora_radio_transmit(
        s_radio, reinterpret_cast<const uint8_t *>(reply), (size_t)written, 0u);
    if (tx == HAL_OK) {
      deb("TX reply sequence=%lu", sequence);
    } else {
      derr("TX reply failed: %s", hal_status_to_string(tx));
    }
  }
  if ((sequence % 20u) == 0u) {
    hal_status_t reinitialize = hal_lora_radio_destroy(s_radio);
    s_radio = nullptr;
    if (reinitialize == HAL_OK) {
      reinitialize = hal_lora_radio_create(&s_hardware, &s_radio);
    }
    if (reinitialize == HAL_OK) {
      reinitialize = hal_lora_radio_configure(s_radio, &s_modem);
    }
    deb("Reinitialize sequence=%lu status=%s", sequence,
        hal_status_to_string(reinitialize));
    if (reinitialize != HAL_OK) {
      s_ready = false;
      return;
    }
  }
  start_responder_receive();
}

#else
void initiator_poll(void) {
  if (!s_waiting_for_reply &&
      (int32_t)(hal_millis() - s_next_transmit_ms) >= 0) {
    char packet[64];
    const uint32_t sequence = ++s_sequence;
    const int written =
        snprintf(packet, sizeof(packet), "JHLORA1 PING %lu %lu",
                 (unsigned long)sequence, (unsigned long)hal_millis());
    if (written <= 0 || (size_t)written >= sizeof(packet)) {
      return;
    }
    const hal_status_t tx = hal_lora_radio_transmit(
        s_radio, reinterpret_cast<const uint8_t *>(packet), (size_t)written,
        0u);
    if (tx != HAL_OK) {
      derr("TX failed: %s", hal_status_to_string(tx));
      s_next_transmit_ms = hal_millis() + kTransmitPeriodMs;
      return;
    }
    deb("TX ping sequence=%lu", (unsigned long)sequence);
    const hal_status_t rx =
        hal_lora_radio_receive_start(s_radio, kReplyTimeoutMs);
    if (rx != HAL_OK) {
      derr("Reply RX start failed: %s", hal_status_to_string(rx));
      s_next_transmit_ms = hal_millis() + kTransmitPeriodMs;
      return;
    }
    s_waiting_for_reply = true;
  }
  if (!s_waiting_for_reply) {
    return;
  }

  uint8_t reply[HAL_LORA_RADIO_MAX_PAYLOAD]{};
  size_t length = 0u;
  hal_lora_packet_info_t info{};
  const hal_status_t rx =
      hal_lora_radio_receive(s_radio, reply, sizeof(reply), &length, &info);
  if (rx == HAL_EAGAIN) {
    return;
  }
  if (rx == HAL_OK) {
    log_packet("RX", reply, length, info);
  } else {
    derr("Reply failed: %s", hal_status_to_string(rx));
  }
  s_waiting_for_reply = false;
  s_next_transmit_ms = hal_millis() + kTransmitPeriodMs;
}
#endif

} // namespace

extern "C" void app_start(void) {
  debugInit();
#ifdef HAL_LORA_EXAMPLE_RESPONDER
  deb("=== JaszczurHAL raw LoRa responder ===");
#else
  deb("=== JaszczurHAL raw LoRa initiator ===");
#endif

  hal_status_t status = hal_lora_radio_config_from_board(&s_hardware);
  if (status == HAL_EUNSUPPORTED) {
    status = external_core1262_hf_config(&s_hardware);
    if (status != HAL_OK) {
      derr("External radio config failed: %s", hal_status_to_string(status));
      return;
    }
    deb("Using external Core1262-HF wiring");
  } else if (status != HAL_OK) {
    derr("Board radio config failed: %s", hal_status_to_string(status));
    return;
  } else {
    deb("Using integrated board radio");
  }

  status = hal_spi_init(s_hardware.spi_bus, s_hardware.spi_miso_pin,
                        s_hardware.spi_mosi_pin, s_hardware.spi_sck_pin);
  if (status == HAL_OK) {
    status = hal_lora_radio_create(&s_hardware, &s_radio);
  }
  s_modem = modem_config(s_hardware);
  if (status == HAL_OK) {
    status = hal_lora_radio_configure(s_radio, &s_modem);
  }
  if (status != HAL_OK) {
    derr("Radio setup failed: %s", hal_status_to_string(status));
    return;
  }
  deb("Radio ready: %lu Hz, SF%u, %ld Hz BW, %d dBm",
      (unsigned long)s_modem.frequency_hz, (unsigned)s_modem.spreading_factor,
      (long)s_modem.bandwidth_hz, (int)s_modem.tx_power_dbm);
  s_ready = true;
#ifdef HAL_LORA_EXAMPLE_RESPONDER
  start_responder_receive();
#else
  s_next_transmit_ms = hal_millis() + 500u;
#endif
}

extern "C" void app_task0(void) {
  if (!s_ready) {
    hal_delay_ms(100u);
    return;
  }
#ifdef HAL_LORA_EXAMPLE_RESPONDER
  responder_poll();
#else
  initiator_poll();
#endif
}
