/**
 * @file app.cpp
 * @brief Raw LoRa ping/pong example for board-declared SX1262 radios.
 */

#include <hal/core/hal_app.h>
#include <hal/core/hal_status.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/radio/hal_lora_radio.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_board.h>
#include <hal/system/hal_system.h>
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
bool s_event_ready = false;
hal_lora_radio_event_t s_event{};
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
bool s_probe_complete = false;
hal_status_t s_probe_status = HAL_ESTATE;
uint32_t s_probe_resets = 0u;
uint32_t s_probe_full_calibrations = 0u;
uint32_t s_probe_image_calibrations = 0u;
int16_t s_probe_rssi_dbm = 0;
bool s_probe_cad_detected = false;
bool s_probe_cad_pending = false;
const char *s_probe_stage = "startup";

void record_probe_result(hal_status_t status, uint32_t resets = 0u) {
  s_probe_status = status;
  s_probe_resets = resets;
  s_probe_complete = true;
}
#endif
#ifdef HAL_LED_BUILTIN
constexpr uint32_t kReceiveLedPulseMs = UINT32_C(120);
uint32_t s_led_off_ms = 0u;

void status_led_initialize(void) {
  hal_gpio_set_mode(HAL_LED_BUILTIN, HAL_GPIO_OUTPUT_LOW);
}

void status_led_transmit_started(void) {
  s_led_off_ms = 0u;
  hal_gpio_write(HAL_LED_BUILTIN, true);
}

void status_led_transmit_finished(void) {
  s_led_off_ms = 0u;
  hal_gpio_write(HAL_LED_BUILTIN, false);
}

void status_led_receive_pulse(void) {
  hal_gpio_write(HAL_LED_BUILTIN, true);
  s_led_off_ms = hal_millis() + kReceiveLedPulseMs;
}

void status_led_process(void) {
  if (s_led_off_ms != 0u && (int32_t)(hal_millis() - s_led_off_ms) >= 0) {
    s_led_off_ms = 0u;
    hal_gpio_write(HAL_LED_BUILTIN, false);
  }
}
#else
void status_led_initialize(void) {}
void status_led_transmit_started(void) {}
void status_led_transmit_finished(void) {}
void status_led_receive_pulse(void) {}
void status_led_process(void) {}
#endif
#ifndef HAL_LORA_EXAMPLE_RESPONDER
enum class InitiatorState { Idle, Transmitting, Receiving };
InitiatorState s_initiator_state = InitiatorState::Idle;
uint32_t s_next_transmit_ms = 0u;
uint32_t s_sequence = 0u;
uint32_t s_active_sequence = 0u;
#else
enum class ResponderState { Receiving, Transmitting };
ResponderState s_responder_state = ResponderState::Receiving;
uint32_t s_last_received_sequence = 0u;
uint32_t s_reply_sequence = 0u;
#endif

void radio_event_callback(hal_lora_radio_t, const hal_lora_radio_event_t *event,
                          void *) {
  s_event = *event;
  s_event_ready = true;
}

hal_lora_modem_config_t modem_config(const hal_lora_radio_config_t &hardware) {
  hal_lora_modem_config_t modem = hal_lora_default_eu868();
  modem.tx_power_dbm = 10;
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
    s_ready = false;
    return;
  }
  s_responder_state = ResponderState::Receiving;
}

void maintain_responder_radio(uint32_t sequence) {
  if ((sequence % 10u) == 0u) {
    hal_lora_radio_diagnostics_t diagnostics{};
    const hal_status_t status =
        hal_lora_radio_get_diagnostics(s_radio, &diagnostics);
    if (status == HAL_OK) {
      deb("Async diagnostics sequence=%lu irq=%lu callbacks=%lu cancelled=%lu",
          (unsigned long)sequence, (unsigned long)diagnostics.irq_events,
          (unsigned long)diagnostics.callback_events,
          (unsigned long)diagnostics.cancelled_operations);
    }
  }
  if ((sequence % 10u) == 0u) {
    const hal_status_t sleep = hal_lora_radio_sleep(s_radio);
    hal_delay_ms(100u);
    const hal_status_t wake = hal_lora_radio_standby(s_radio);
    deb("Sleep/wake sequence=%lu sleep=%s wake=%s", sequence,
        hal_status_to_string(sleep), hal_status_to_string(wake));
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
    if (reinitialize == HAL_OK) {
      reinitialize = hal_lora_radio_set_event_callback(
          s_radio, radio_event_callback, nullptr);
    }
    deb("Reinitialize sequence=%lu status=%s", sequence,
        hal_status_to_string(reinitialize));
    if (reinitialize != HAL_OK) {
      s_ready = false;
    }
  }
}

void responder_receive_ready(void) {
  uint8_t packet[HAL_LORA_RADIO_MAX_PAYLOAD]{};
  size_t length = 0u;
  hal_lora_packet_info_t info{};
  const hal_status_t status =
      hal_lora_radio_receive(s_radio, packet, sizeof(packet), &length, &info);
  if (status != HAL_OK) {
    derr("RX failed: %s", hal_status_to_string(status));
    (void)hal_lora_radio_cancel(s_radio);
    start_responder_receive();
    return;
  }
  status_led_receive_pulse();
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
  (void)hal_lora_radio_cancel(s_radio);
  if (written > 0 && (size_t)written < sizeof(reply)) {
    const hal_status_t tx = hal_lora_radio_transmit_start(
        s_radio, reinterpret_cast<const uint8_t *>(reply), (size_t)written);
    if (tx != HAL_OK) {
      derr("TX reply failed: %s", hal_status_to_string(tx));
      start_responder_receive();
      return;
    }
    s_reply_sequence = (uint32_t)sequence;
    s_responder_state = ResponderState::Transmitting;
    status_led_transmit_started();
  } else {
    start_responder_receive();
  }
}

void responder_handle_event(const hal_lora_radio_event_t &event) {
  if (event.type == HAL_LORA_RADIO_EVENT_CANCELLED &&
      event.operation == HAL_LORA_OPERATION_KIND_RECEIVE &&
      s_responder_state == ResponderState::Transmitting) {
    /* Continuous RX is deliberately cancelled before sending the reply. */
    return;
  }
  if (event.type == HAL_LORA_RADIO_EVENT_RX_READY &&
      s_responder_state == ResponderState::Receiving) {
    responder_receive_ready();
    return;
  }
  if (event.type == HAL_LORA_RADIO_EVENT_TX_COMPLETE &&
      s_responder_state == ResponderState::Transmitting) {
    status_led_transmit_finished();
    deb("TX reply sequence=%lu", (unsigned long)s_reply_sequence);
    maintain_responder_radio(s_reply_sequence);
    if (s_ready) {
      start_responder_receive();
    }
    return;
  }
  if (event.operation == HAL_LORA_OPERATION_KIND_TRANSMIT) {
    status_led_transmit_finished();
  }
  derr("Radio event failed: %s", hal_status_to_string(event.result));
  if (s_responder_state == ResponderState::Receiving) {
    uint8_t ignored = 0u;
    size_t ignored_length = 0u;
    (void)hal_lora_radio_receive(s_radio, &ignored, sizeof(ignored),
                                 &ignored_length, nullptr);
  }
  (void)hal_lora_radio_cancel(s_radio);
  (void)hal_lora_radio_standby(s_radio);
  start_responder_receive();
}

#else
void schedule_next_transmit(void) {
  s_initiator_state = InitiatorState::Idle;
  s_next_transmit_ms = hal_millis() + kTransmitPeriodMs;
}

void initiator_start_transmit(void) {
  if (s_initiator_state == InitiatorState::Idle &&
      (int32_t)(hal_millis() - s_next_transmit_ms) >= 0) {
    char packet[64];
    const uint32_t sequence = ++s_sequence;
    const int written =
        snprintf(packet, sizeof(packet), "JHLORA1 PING %lu %lu",
                 (unsigned long)sequence, (unsigned long)hal_millis());
    if (written <= 0 || (size_t)written >= sizeof(packet)) {
      return;
    }
    const hal_status_t tx = hal_lora_radio_transmit_start(
        s_radio, reinterpret_cast<const uint8_t *>(packet), (size_t)written);
    if (tx != HAL_OK) {
      derr("TX failed: %s", hal_status_to_string(tx));
      schedule_next_transmit();
      return;
    }
    s_active_sequence = sequence;
    s_initiator_state = InitiatorState::Transmitting;
    status_led_transmit_started();
  }
}

void initiator_handle_event(const hal_lora_radio_event_t &event) {
  if (event.type == HAL_LORA_RADIO_EVENT_TX_COMPLETE &&
      s_initiator_state == InitiatorState::Transmitting) {
    status_led_transmit_finished();
    hal_lora_operation_status_t tx_status{};
    if (hal_lora_radio_get_tx_status(s_radio, &tx_status) != HAL_OK ||
        tx_status.state != HAL_LORA_OPERATION_SUCCEEDED) {
      derr("TX completion status mismatch");
      schedule_next_transmit();
      return;
    }
    deb("TX ping sequence=%lu", (unsigned long)s_active_sequence);
    const hal_status_t rx =
        hal_lora_radio_receive_start(s_radio, kReplyTimeoutMs);
    if (rx != HAL_OK) {
      derr("Reply RX start failed: %s", hal_status_to_string(rx));
      schedule_next_transmit();
      return;
    }
    s_initiator_state = InitiatorState::Receiving;
    return;
  }
  if (event.type != HAL_LORA_RADIO_EVENT_RX_READY ||
      s_initiator_state != InitiatorState::Receiving) {
    if (event.operation == HAL_LORA_OPERATION_KIND_TRANSMIT) {
      status_led_transmit_finished();
    }
    derr("Reply failed: %s", hal_status_to_string(event.result));
    if (s_initiator_state == InitiatorState::Receiving) {
      uint8_t ignored = 0u;
      size_t ignored_length = 0u;
      (void)hal_lora_radio_receive(s_radio, &ignored, sizeof(ignored),
                                   &ignored_length, nullptr);
    }
    schedule_next_transmit();
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
    status_led_receive_pulse();
    log_packet("RX", reply, length, info);
  } else {
    derr("Reply failed: %s", hal_status_to_string(rx));
  }
  schedule_next_transmit();
}
#endif

} // namespace

extern "C" void app_start(void) {
  debugInit();
  status_led_initialize();
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
  deb("=== JaszczurHAL LoRa wiring probe (no RF transmit) ===");
#elif defined(HAL_LORA_EXAMPLE_RESPONDER)
  deb("=== JaszczurHAL raw LoRa responder ===");
#else
  deb("=== JaszczurHAL raw LoRa initiator ===");
#endif

  hal_status_t status = hal_lora_radio_config_from_board(&s_hardware);
  if (status == HAL_EUNSUPPORTED) {
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
    s_probe_stage = "board-config-unsupported";
#endif
    derr("Selected board profile does not declare an SX1262 radio");
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
    record_probe_result(status);
#endif
    return;
  } else if (status != HAL_OK) {
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
    s_probe_stage = "board-config";
#endif
    derr("Board radio config failed: %s", hal_status_to_string(status));
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
    record_probe_result(status);
#endif
    return;
  } else {
    deb("Using board-declared radio wiring");
  }
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
  s_probe_stage = "spi-init";
#endif
  status = hal_spi_init(s_hardware.spi_bus, s_hardware.spi_miso_pin,
                        s_hardware.spi_mosi_pin, s_hardware.spi_sck_pin);
  if (status == HAL_OK) {
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
    s_probe_stage = "radio-create";
#endif
    status = hal_lora_radio_create(&s_hardware, &s_radio);
  }
  s_modem = modem_config(s_hardware);
  if (status == HAL_OK) {
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
    s_probe_stage = "radio-configure";
#endif
    status = hal_lora_radio_configure(s_radio, &s_modem);
  }
  if (status == HAL_OK) {
#ifndef HAL_LORA_EXAMPLE_PROBE_ONLY
    status = hal_lora_radio_set_event_callback(s_radio, radio_event_callback,
                                               nullptr);
#endif
  }
  if (status != HAL_OK) {
    derr("Radio setup failed: %s", hal_status_to_string(status));
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
    record_probe_result(status);
#endif
    return;
  }
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
  hal_lora_radio_capabilities_t capabilities{};
  s_probe_stage = "capabilities";
  status = hal_lora_radio_get_capabilities(s_radio, &capabilities);
  if (status == HAL_OK && (!capabilities.supports_channel_activity_detection ||
                           !capabilities.supports_instant_rssi ||
                           !capabilities.supports_explicit_calibration)) {
    status = HAL_EUNSUPPORTED;
  }
  if (status == HAL_OK) {
    s_probe_stage = "calibration";
    status = hal_lora_radio_calibrate(s_radio);
  }
  if (status == HAL_OK) {
    s_probe_stage = "receive-start";
    status = hal_lora_radio_receive_start_continuous(s_radio);
  }
  if (status == HAL_OK) {
    hal_delay_ms(20u);
    s_probe_stage = "instant-rssi";
    status = hal_lora_radio_get_instant_rssi(s_radio, &s_probe_rssi_dbm);
  }
  if (status == HAL_OK) {
    s_probe_stage = "receive-cancel";
    status = hal_lora_radio_cancel(s_radio);
  }
  if (status == HAL_OK) {
    s_probe_stage = "event-callback";
    status = hal_lora_radio_set_event_callback(s_radio, radio_event_callback,
                                               nullptr);
  }
  if (status == HAL_OK) {
    s_probe_stage = "cad-start";
    status = hal_lora_radio_channel_activity_detect_start(s_radio, 250u);
  }
  if (status != HAL_OK) {
    derr("JHLORA1 PROBE FAIL: stage=%s status=%s", s_probe_stage,
         hal_status_to_string(status));
    record_probe_result(status);
    return;
  }
  s_probe_stage = "cad-process";
  s_probe_cad_pending = true;
  return;
#endif
  deb("Radio ready: %lu Hz, SF%u, %ld Hz BW, %d dBm",
      (unsigned long)s_modem.frequency_hz, (unsigned)s_modem.spreading_factor,
      (long)s_modem.bandwidth_hz, (int)s_modem.tx_power_dbm);
  deb("Async DIO1 event loop enabled");
#ifdef HAL_LED_BUILTIN
  deb("Status LED: solid during TX, 120 ms pulse after RX");
#endif
  s_ready = true;
#ifdef HAL_LORA_EXAMPLE_RESPONDER
  start_responder_receive();
#else
  s_next_transmit_ms = hal_millis() + 500u;
#endif
}

extern "C" void app_task0(void) {
#ifdef HAL_LORA_EXAMPLE_PROBE_ONLY
  static uint32_t next_report_ms = 0u;
  const uint32_t now = hal_millis();
  if (s_probe_cad_pending) {
    const hal_status_t process = hal_lora_radio_process(s_radio);
    hal_lora_channel_activity_status_t cad{};
    hal_status_t status =
        hal_lora_radio_get_channel_activity_status(s_radio, &cad);
    if (status == HAL_OK && process != HAL_OK && process != HAL_EAGAIN) {
      status = process;
    }
    if (status != HAL_OK) {
      s_probe_cad_pending = false;
      record_probe_result(status);
    } else if (cad.state != HAL_LORA_OPERATION_IN_PROGRESS) {
      s_probe_cad_pending = false;
      if (cad.state != HAL_LORA_OPERATION_SUCCEEDED || !s_event_ready ||
          s_event.type != HAL_LORA_RADIO_EVENT_CHANNEL_ACTIVITY_COMPLETE) {
        record_probe_result(cad.result == HAL_OK ? HAL_ESTATE : cad.result);
      } else {
        s_probe_cad_detected = cad.detected;
        hal_lora_radio_diagnostics_t diagnostics{};
        status = hal_lora_radio_get_diagnostics(s_radio, &diagnostics);
        if (status == HAL_OK) {
          s_probe_full_calibrations = diagnostics.full_calibrations;
          s_probe_image_calibrations = diagnostics.image_calibrations;
          s_probe_stage = "complete";
          record_probe_result(HAL_OK, diagnostics.resets);
        } else {
          s_probe_stage = "diagnostics";
          record_probe_result(status);
        }
      }
    }
  }
  if (s_probe_complete && (int32_t)(now - next_report_ms) >= 0) {
    if (s_probe_status == HAL_OK) {
      deb("JHLORA1 PROBE PASS: SX1262 "
          "capabilities/calibration/RSSI/CAD/standby; resets=%lu RSSI=%d dBm "
          "CAD=%s full-cal=%lu image-cal=%lu; RF transmit disabled",
          (unsigned long)s_probe_resets, (int)s_probe_rssi_dbm,
          s_probe_cad_detected ? "detected" : "clear",
          (unsigned long)s_probe_full_calibrations,
          (unsigned long)s_probe_image_calibrations);
    } else {
      derr("JHLORA1 PROBE FAIL: stage=%s status=%s; RF transmit disabled",
           s_probe_stage, hal_status_to_string(s_probe_status));
    }
    next_report_ms = now + UINT32_C(2000);
  }
  hal_delay_ms(10u);
  return;
#endif
  status_led_process();
  if (!s_ready) {
    hal_delay_ms(100u);
    return;
  }
  const hal_status_t process = hal_lora_radio_process(s_radio);
  if (process != HAL_OK && process != HAL_EAGAIN && !s_event_ready) {
    derr("Radio process failed: %s", hal_status_to_string(process));
  }
  if (s_event_ready) {
    const hal_lora_radio_event_t event = s_event;
    s_event_ready = false;
#ifdef HAL_LORA_EXAMPLE_RESPONDER
    responder_handle_event(event);
#else
    initiator_handle_event(event);
#endif
  }
#ifndef HAL_LORA_EXAMPLE_RESPONDER
  initiator_start_transmit();
#endif
}
