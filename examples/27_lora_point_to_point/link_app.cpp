/**
 * @file link_app.cpp
 * @brief Addressed, acknowledged and fragmented LoRa link example.
 */

#ifdef HAL_ENABLE_LORA_LINK

#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/radio/hal_lora_link.h>
#include <hal/radio/hal_lora_radio.h>
#include <hal/security/hal_crc.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>
#include <tools.h>

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint16_t kInitiatorAddress = UINT16_C(0x1001);
constexpr uint16_t kResponderAddress = UINT16_C(0x1002);
constexpr uint8_t kApplicationPort = 1u;
constexpr uint32_t kLfTestFrequencyHz = UINT32_C(434000000);
constexpr uint32_t kTransmitPeriodMs = UINT32_C(5000);
constexpr size_t kMessageLength = 360u;

#ifdef HAL_LORA_LINK_EXAMPLE_RESPONDER
constexpr uint16_t kLocalAddress = kResponderAddress;
constexpr uint16_t kPeerAddress = kInitiatorAddress;
#else
constexpr uint16_t kLocalAddress = kInitiatorAddress;
constexpr uint16_t kPeerAddress = kResponderAddress;
#endif

hal_lora_radio_t s_radio = nullptr;
hal_lora_link_t s_link = nullptr;
bool s_ready = false;
#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
bool s_send_active = false;
uint32_t s_application_sequence = 0u;
uint32_t s_next_transmit_ms = 0u;
#endif

hal_lora_modem_config_t modem_config(const hal_lora_radio_config_t &hardware) {
  hal_lora_modem_config_t modem = hal_lora_default_eu868();
  modem.tx_power_dbm = 10;
  if (hardware.hardware.sx126x.max_frequency_hz < UINT32_C(800000000)) {
    /* Deliberate LF hardware-test configuration, not a regulatory preset. */
    modem.frequency_hz = kLfTestFrequencyHz;
  }
  return modem;
}

uint32_t example_session_id(void) {
  uint8_t seed[HAL_DEVICE_UID_BYTES + sizeof(uint32_t) + sizeof(uint16_t)]{};
  (void)hal_get_device_uid(seed);
  const uint32_t started_us = hal_micros();
  memcpy(&seed[HAL_DEVICE_UID_BYTES], &started_us, sizeof(started_us));
  memcpy(&seed[HAL_DEVICE_UID_BYTES + sizeof(started_us)], &kLocalAddress,
         sizeof(kLocalAddress));
  uint32_t session_id = hal_crc32(seed, sizeof(seed));
  if (session_id == 0u) {
    session_id = (uint32_t)kLocalAddress;
  }
  return session_id;
}

void receive_messages(void) {
  uint8_t message[HAL_LORA_LINK_MAX_MESSAGE_SIZE + 1u]{};
  size_t length = 0u;
  hal_lora_link_message_info_t info{};
  const hal_status_t status = hal_lora_link_receive(
      s_link, message, HAL_LORA_LINK_MAX_MESSAGE_SIZE, &length, &info);
  if (status == HAL_EAGAIN) {
    return;
  }
  if (status != HAL_OK) {
    derr("Link receive failed: %s", hal_status_to_string(status));
    return;
  }
  message[length] = '\0';
  deb("RX link src=0x%04X dst=0x%04X seq=%lu fragments=%u RSSI=%d dBm "
      "SNR=%d dB bytes=%u prefix='%.48s'",
      (unsigned)info.source, (unsigned)info.destination,
      (unsigned long)info.sequence, (unsigned)info.fragment_count,
      (int)info.packet.rssi_dbm, (int)info.packet.snr_db, (unsigned)length,
      reinterpret_cast<const char *>(message));
}

#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
void start_message(void) {
  if (s_send_active || (int32_t)(hal_millis() - s_next_transmit_ms) < 0) {
    return;
  }
  uint8_t message[kMessageLength]{};
  const uint32_t sequence = ++s_application_sequence;
  const int prefix_length =
      snprintf(reinterpret_cast<char *>(message), sizeof(message),
               "JHLINK1 message=%lu uptime=%lu fragmented payload: ",
               (unsigned long)sequence, (unsigned long)hal_millis());
  if (prefix_length <= 0 || (size_t)prefix_length >= sizeof(message)) {
    return;
  }
  for (size_t index = (size_t)prefix_length; index < sizeof(message); ++index) {
    message[index] = (uint8_t)('A' + (index % 26u));
  }
  const hal_status_t status = hal_lora_link_send_start(
      s_link, kPeerAddress, kApplicationPort, message, sizeof(message), true);
  if (status == HAL_OK) {
    s_send_active = true;
    deb("TX link started: application=%lu bytes=%u destination=0x%04X",
        (unsigned long)sequence, (unsigned)sizeof(message),
        (unsigned)kPeerAddress);
  } else {
    derr("Link send start failed: %s", hal_status_to_string(status));
    s_next_transmit_ms = hal_millis() + kTransmitPeriodMs;
  }
}

void report_send_completion(void) {
  if (!s_send_active) {
    return;
  }
  hal_lora_link_send_status_t send{};
  const hal_status_t status = hal_lora_link_get_send_status(s_link, &send);
  if (status != HAL_OK || send.state == HAL_LORA_OPERATION_IN_PROGRESS) {
    return;
  }
  if (send.state == HAL_LORA_OPERATION_SUCCEEDED) {
    deb("TX link acknowledged: sequence=%lu attempts=%u fragments=%u",
        (unsigned long)send.sequence, (unsigned)send.attempts,
        (unsigned)send.fragment_count);
  } else {
    derr("TX link failed: sequence=%lu result=%s attempts=%u",
         (unsigned long)send.sequence, hal_status_to_string(send.result),
         (unsigned)send.attempts);
  }
  s_send_active = false;
  s_next_transmit_ms = hal_millis() + kTransmitPeriodMs;
}
#endif

} // namespace

extern "C" void app_start(void) {
  debugInit();
#ifdef HAL_LORA_LINK_EXAMPLE_RESPONDER
  deb("=== JaszczurHAL reliable LoRa link responder ===");
#else
  deb("=== JaszczurHAL reliable LoRa link initiator ===");
#endif

  hal_lora_radio_config_t hardware{};
  hal_status_t status = hal_lora_radio_config_from_board(&hardware);
  if (status == HAL_OK) {
    status = hal_spi_init(hardware.spi_bus, hardware.spi_miso_pin,
                          hardware.spi_mosi_pin, hardware.spi_sck_pin);
  }
  if (status == HAL_OK) {
    status = hal_lora_radio_create(&hardware, &s_radio);
  }
  if (status == HAL_OK) {
    const hal_lora_modem_config_t modem = modem_config(hardware);
    status = hal_lora_radio_configure(s_radio, &modem);
  }
  const uint32_t session_id = example_session_id();
  if (status == HAL_OK) {
    hal_lora_link_config_t config =
        hal_lora_link_config_defaults(s_radio, kLocalAddress, session_id);
    status = hal_lora_link_create(&config, &s_link);
  }
  if (status != HAL_OK) {
    derr("Reliable link setup failed: %s", hal_status_to_string(status));
    return;
  }
  deb("Link ready: local=0x%04X peer=0x%04X session=0x%08lX",
      (unsigned)kLocalAddress, (unsigned)kPeerAddress,
      (unsigned long)session_id);
  s_ready = true;
#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
  s_next_transmit_ms = hal_millis() + 500u;
#endif
}

extern "C" void app_task0(void) {
  if (!s_ready) {
    hal_delay_ms(100u);
    return;
  }
  const hal_status_t status = hal_lora_link_process(s_link);
  if (status != HAL_OK && status != HAL_EAGAIN && status != HAL_IGNORED) {
    derr("Link process failed: %s", hal_status_to_string(status));
  }
  receive_messages();
#ifndef HAL_LORA_LINK_EXAMPLE_RESPONDER
  report_send_completion();
  start_message();
#endif
}

#endif /* HAL_ENABLE_LORA_LINK */
