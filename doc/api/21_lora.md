# Raw LoRa radio API

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

`hal_lora_radio` is the provider-neutral raw packet-radio facade. One selected
family provider integrates SX1261/SX1262 through the pinned official Semtech
SX126x driver or SX1276/SX1278 through the HAL-owned SX127x register driver.
Both use only HAL SPI, GPIO, timing and mutex services and build for RP2040,
RP2350 and STM32G474; the deterministic mock provides host tests.

The API supports blocking and asynchronous transmit, asynchronous receive,
DIO-driven task-context processing, channel activity detection (CAD), current
RSSI reads, explicit calibration, capabilities, callbacks, cancellation and
explicit operation states. Applications own hardware and modem descriptors.
Each opaque handle owns its TX/RX packet buffers, state, mutex and diagnostics.

## Enable the module

Select exactly one family provider in `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_SX126X
/* or: #define HAL_ENABLE_SX127X */
```

The feature registry propagates `HAL_ENABLE_LORA` and `HAL_ENABLE_SPI`.
Selecting `HAL_ENABLE_LORA` alone or both family providers is rejected.

The following tunables are available before `hal_config.h` is included:

| Macro | Default | Valid range | Purpose |
|---|---:|---:|---|
| `HAL_LORA_RADIO_MAX_INSTANCES` | 2 | 1..255 | Number of generation-tagged static handle slots |
| `HAL_LORA_SX126X_BUSY_TIMEOUT_MS` | 1000 | 1..60000 | Maximum wait for the SX126x BUSY line around a command |
| `HAL_LORA_SX127X_RESET_SETTLE_MS` | 10 | 5..1000 | Delay after releasing SX127x reset before its version probe |

## Model maturity

SX1262 is physically validated on the boards and fixtures described below.
SX1261, SX1276 and SX1278 are `experimental`: their integration has passed
deterministic host tests plus RP2040 and STM32G474 compile/link gates, but no
physical radio was available. They deliberately add no board profile or
runtime capability. Promotion requires a documented hardware test for the
specific model.

The Semtech SX127x implementations available in LoRaMac-node and LoRa Basics
Modem are coupled to their respective board, timer and stack layers. Pulling
either complete stack solely for raw-radio register access would create an
unnecessary dependency boundary. JaszczurHAL therefore owns the compact
SX127x provider; it follows the public register interface and remains behind
the same provider-neutral facade.

## Hardware ownership

Creating a radio initializes the selected SPI controller with the descriptor's
pins. The facade serializes each command with the HAL bus lock and applies the
device clock, mode 0 and MSB-first settings for each transaction. It owns the
radio CS, RESET and family-specific control behavior described by the hardware
descriptor. SX126x owns BUSY, DIO1 and its RF-switch/TCXO topology. SX127x has
a separate descriptor for DIO0 through DIO2, optional RX/TX switch GPIOs,
optional TCXO enable and RFO versus PA_BOOST selection.

The SPI controller may be shared with other HAL devices. The provider attaches
rising-edge interrupts during create; their ISR only records pending work.
SPI commands and callbacks run later from task context. Destroying a radio
detaches the family-specific DIO lines, returns the radio to a safe power state
and releases its handle without deinitializing the shared bus.

## Integrated board configuration

`hal_lora_radio_config_from_board()` copies the active board profile's radio
facts. It returns `HAL_EUNSUPPORTED` when the selected board has no declared
radio, whether integrated or part of a fixed composite fixture.

```c
hal_lora_radio_config_t hardware;
hal_status_t status = hal_lora_radio_config_from_board(&hardware);
if (status != HAL_OK) {
  return status;
}

status = hal_spi_init(hardware.spi_bus, hardware.spi_miso_pin,
                      hardware.spi_mosi_pin, hardware.spi_sck_pin);
```

The `rp2040-lora-lf` profile describes the integrated SX1262 on the Waveshare
RP2040-LoRa-LF board, including its LF frequency limits. Choose an explicit
frequency for the intended hardware test and regulatory environment.

The experimental `pico-core1262-hf` and
`nucleo-g474re-core1262-hf` profiles describe the two fixed project fixtures
with external Waveshare Core1262-HF modules. Both have passed no-transmit
CAD/RSSI/calibration probes and bidirectional OTA tests. The Nucleo fixture
uses SPI2 on PB13/PB14/PB15 so the built-in LD2 and `HAL_LED_BUILTIN` remain
available on PA5.

## External Waveshare Core1262-HF

For the fixed project wiring, select `pico-core1262-hf` or
`nucleo-g474re-core1262-hf` and use
`hal_lora_radio_config_from_board()`. For a different application-owned wiring,
select the plain host profile and use
`hal_lora_sx126x_core1262_hf_defaults()`. The helper fills the module-owned
electrical profile: dual RXEN/TXEN control, DCDC, DIO3-controlled 1.8 V TCXO,
startup delay, RF range, SPI limit and output-power limits. Host bus and pin
assignments remain application input. Assign RXEN to `rf_switch_pin_a` and
TXEN to `rf_switch_pin_b`; the helper installs the documented level table.

```c
hal_lora_radio_config_t hardware = {0};
hardware.model = HAL_LORA_RADIO_SX1262;
hardware.spi_bus = 0;
hardware.spi_miso_pin = 16;
hardware.spi_mosi_pin = 19;
hardware.spi_sck_pin = 18;
hardware.cs_pin = 17;
hardware.spi_clock_hz = HAL_LORA_SPI_CLOCK_DEFAULT_HZ;

hal_lora_sx126x_hardware_config_t *sx = &hardware.hardware.sx126x;
hal_status_t status = hal_lora_sx126x_core1262_hf_defaults(sx);
if (status != HAL_OK) {
  return status;
}
sx->reset_pin = 20;
sx->busy_pin = 21;
sx->dio1_pin = 22;
sx->rf_switch_pin_a = 10;
sx->rf_switch_pin_b = 11;
```

`HAL_LORA_PIN_NONE` marks a GPIO that is intentionally unconnected. Required
pins and RF-switch topology are validated during `hal_lora_radio_create()`.

For application-owned SX127x wiring, fill `hardware.sx127x` directly. DIO0 and
RESET are required; DIO1/DIO2, RX/TX switch GPIOs and TCXO enable may use
`HAL_LORA_PIN_NONE`. The selected PA path constrains the accepted power range:
RFO supports -4..15 dBm and PA_BOOST supports 2..20 dBm. SX1278 is limited to
137..525 MHz, while an SX1276 descriptor may extend to 960 MHz. Module limits
may narrow these chip limits.

```c
hal_lora_radio_config_t hardware = {0};
hardware.model = HAL_LORA_RADIO_SX1276;
hardware.spi_bus = 0;
hardware.spi_miso_pin = 16;
hardware.spi_mosi_pin = 19;
hardware.spi_sck_pin = 18;
hardware.cs_pin = 17;
hardware.spi_clock_hz = UINT32_C(4000000);
hardware.hardware.sx127x.reset_pin = 20;
hardware.hardware.sx127x.dio0_pin = 21;
hardware.hardware.sx127x.dio1_pin = 22;
hardware.hardware.sx127x.dio2_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.rf_switch_rx_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.rf_switch_tx_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.tcxo_enable_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.pa_output = HAL_LORA_SX127X_PA_BOOST;
hardware.hardware.sx127x.min_frequency_hz = UINT32_C(850000000);
hardware.hardware.sx127x.max_frequency_hz = UINT32_C(930000000);
hardware.hardware.sx127x.max_spi_clock_hz = UINT32_C(10000000);
hardware.hardware.sx127x.min_tx_power_dbm = 2;
hardware.hardware.sx127x.max_tx_power_dbm = 20;
```

## Lifecycle and modem configuration

The normal sequence is SPI initialization, create/probe, modem configure,
packet operations and destroy:

```c
hal_lora_radio_t radio = NULL;
hal_status_t status = hal_lora_radio_create(&hardware, &radio);
if (status != HAL_OK) {
  return status;
}

hal_lora_modem_config_t modem = hal_lora_default_eu868();
status = hal_lora_radio_configure(radio, &modem);
if (status != HAL_OK) {
  (void)hal_lora_radio_destroy(radio);
  return status;
}
```

Handles carry a generation tag. Calls through a destroyed or otherwise stale
handle return `HAL_EUNINIT`. `hal_lora_radio_create()` returns `HAL_ENOMEM`
when the configured static pool is full. Active TX/RX/CAD must be completed or
cancelled before destroy.

Modem validation covers model and module frequency/power limits, supported LoRa
bandwidths, coding rates 5 through 8, preamble, header mode and implicit payload
length. SX126x accepts spreading factors 5 through 12; SX127x accepts 6 through
12.

Three EU868 technical presets use 868.1 MHz, 125 kHz, coding rate 4/5, explicit
header, CRC, an eight-symbol preamble and 14 dBm:

| Helper | Spreading factor | Intended starting point |
|---|---:|---|
| `hal_lora_default_fast_eu868()` | SF7 | Short airtime |
| `hal_lora_default_eu868()` | SF9 | Balanced link |
| `hal_lora_default_long_range_eu868()` | SF12 | Longer airtime and link budget |

These values are technical starting points. The application remains
responsible for legal frequency, output power, antenna, bandwidth and duty
cycle in its jurisdiction.

An LF device uses a deliberate raw configuration rather than a global preset:

```c
hal_lora_modem_config_t modem = hal_lora_default_eu868();
modem.frequency_hz = UINT32_C(434000000);
modem.tx_power_dbm = 10;
```

## Blocking transmit

`hal_lora_radio_transmit()` copies 1 through 255 bytes into handle-owned
storage, starts TX and waits for TX-done or timeout. A timeout value of zero
selects a deadline calculated from packet airtime plus an internal margin.

```c
static const uint8_t payload[] = "ping";
status = hal_lora_radio_transmit(radio, payload, sizeof(payload) - 1u, 0u);
```

Successful and timed-out operations return the handle to standby. A bus or
device failure moves it to `HAL_LORA_RADIO_STATE_ERROR`; a successful
`hal_lora_radio_configure()` can restore the configured standby state.

The blocking function uses the same start/process state machine as asynchronous
TX. It does not retain the caller's payload buffer.

## Asynchronous operation and callbacks

`hal_lora_radio_transmit_start()` copies the payload, starts the radio and
returns before TX-done. Its deadline is calculated from time-on-air plus the
driver margin. Call `hal_lora_radio_process()` from `app_task0()` or a FreeRTOS
task and inspect the stable status snapshot:

```c
static void radio_event(hal_lora_radio_t radio,
                        const hal_lora_radio_event_t *event,
                        void *user_data) {
  (void)radio;
  (void)user_data;
  if (event->type == HAL_LORA_RADIO_EVENT_TX_COMPLETE) {
    /* Start RX or schedule the next packet. */
  }
}

status = hal_lora_radio_set_event_callback(radio, radio_event, NULL);
if (status == HAL_OK) {
  status = hal_lora_radio_transmit_start(radio, payload, payload_length);
}

for (;;) {
  status = hal_lora_radio_process(radio);
  if (status != HAL_OK && status != HAL_EAGAIN) {
    /* The callback receives the same terminal result. */
  }

  hal_lora_operation_status_t tx;
  if (hal_lora_radio_get_tx_status(radio, &tx) == HAL_OK &&
      tx.state == HAL_LORA_OPERATION_SUCCEEDED) {
    break;
  }
  hal_idle();
}
```

TX status distinguishes `IDLE`, `IN_PROGRESS`, `SUCCEEDED`, `TIMED_OUT`,
`CANCELLED` and `FAILED`; `result` retains the matching `hal_status_t`.
Callbacks report TX completion, RX readiness, CAD completion, timeout,
cancellation or error, plus the operation kind and task-context timestamp.
CAD completion also carries `channel_activity_detected`. Callbacks are invoked
synchronously by `hal_lora_radio_process()`, outside internal locks, and may
call radio APIs. Passing a null callback clears registration.

`hal_lora_radio_cancel()` stops active TX, bounded RX, continuous RX or CAD and
enters standby. Cancellation is explicit: power-state and destroy operations
return `HAL_EBUSY` while a radio operation is active.

## Polling receive

Start one bounded receive window, service provider IRQs and copy the completed
packet:

```c
status = hal_lora_radio_receive_start(radio, 1500u);
while (status == HAL_OK) {
  const hal_status_t process = hal_lora_radio_process(radio);
  if (process != HAL_OK && process != HAL_EAGAIN) {
    status = process;
    break;
  }
  uint8_t packet[HAL_LORA_RADIO_MAX_PAYLOAD];
  size_t length = 0;
  hal_lora_packet_info_t info;
  status = hal_lora_radio_receive(radio, packet, sizeof(packet),
                                  &length, &info);
  if (status == HAL_EAGAIN) {
    hal_idle();
    continue;
  }
  if (status == HAL_OK) {
    /* packet[0..length), info.rssi_dbm, info.snr_db */
  }
  break;
}
```

`hal_lora_radio_receive_start_continuous()` keeps the receiver active after a
packet. Call `hal_lora_radio_cancel()` to stop continuous receive.
`hal_lora_radio_receive()` remains a compatibility polling entry: it services
one pending provider IRQ/timeout step itself before copying a packet.

Receive results have the following meanings:

| Status | Meaning |
|---|---|
| `HAL_OK` | One packet and its metadata were copied |
| `HAL_EAGAIN` | RX remains in progress |
| `HAL_ETIMEOUT` | A bounded receive window expired; state returned to standby |
| `HAL_EPROTO` | The radio reported a CRC error |
| `HAL_EOVERFLOW` | The packet was consumed, `out_length` contains its full size and the caller buffer contains the prefix that fit |

`hal_lora_packet_info_t` reports packet RSSI, SNR, signal RSSI, receive
timestamp and CRC validity.

## Capabilities, current RSSI, CAD and calibration

`hal_lora_radio_get_capabilities()` reports provider-neutral hardware limits
and optional operations. SX126x exposes continuous RX, CAD, current RSSI and
explicit calibration. SX127x exposes continuous RX, CAD and current RSSI, and
reports explicit calibration as unsupported:

```c
hal_lora_radio_capabilities_t capabilities;
status = hal_lora_radio_get_capabilities(radio, &capabilities);
```

Call `hal_lora_radio_calibrate()` only when
`supports_explicit_calibration` is true. SX127x returns `HAL_EUNSUPPORTED`
without changing the stable standby state.

`hal_lora_radio_get_instant_rssi()` reads the current receiver RSSI and is
valid only while the radio is in RX mode. It returns `HAL_ESTATE` in standby,
TX, CAD or sleep. The read updates `last_instant_rssi_dbm` and
`instant_rssi_reads` diagnostics.

CAD is an asynchronous operation serviced by the same DIO/process lifecycle
as TX and RX:

```c
status = hal_lora_radio_channel_activity_detect_start(radio, 100u);
while (status == HAL_OK) {
  const hal_status_t process = hal_lora_radio_process(radio);
  if (process != HAL_OK && process != HAL_EAGAIN) {
    status = process;
    break;
  }

  hal_lora_channel_activity_status_t cad;
  status = hal_lora_radio_get_channel_activity_status(radio, &cad);
  if (status == HAL_OK && cad.state == HAL_LORA_OPERATION_IN_PROGRESS) {
    hal_idle();
    continue;
  }
  if (status == HAL_OK && cad.state == HAL_LORA_OPERATION_SUCCEEDED) {
    /* cad.detected distinguishes an occupied channel from a clear channel. */
  }
  break;
}
```

A zero CAD timeout is invalid. Timeout, cancellation and provider failures use
the common operation-state and callback semantics. CAD is a channel observation,
not a regulatory listen-before-talk policy; the application remains responsible
for any required access procedure.

The SX126x provider performs full calibration during create and band-aware
image calibration during configure. It caches the calibrated frequency range,
so repeated configuration inside one range does not issue redundant image
calibration. `hal_lora_radio_calibrate()` explicitly repeats full calibration
and image calibration for the configured frequency while in standby. It
returns `HAL_ESTATE` before modem configuration or while another operation is
active.
SX127x capabilities report explicit calibration as unsupported, and its
provider returns `HAL_EUNSUPPORTED` for that optional operation.

## State, power and diagnostics

`hal_lora_radio_get_state()` returns the stable public state. The explicit
state machine uses
`STANDBY`, `RX`, `TX`, `CAD`, `SLEEP` and `ERROR`.

`hal_lora_radio_sleep()` enters the selected radio's sleep configuration.
`hal_lora_radio_standby()` wakes a sleeping or error-state radio. Active RX/TX
is ended through `hal_lora_radio_cancel()`.

`hal_lora_radio_get_diagnostics()` copies counters for transmitted/received
packets, CRC/header errors, TX/RX timeouts, cancellations, operation/bus errors,
DIO events, callback delivery, dropped packets/events and resets. CAD counters
distinguish checks, detected, clear and timed-out results. Calibration counters
distinguish full and image calibration and retain the cached frequency range.
Diagnostics also report the latest packet RSSI, signal RSSI, current RSSI, SNR,
error, event timestamp and state-change timestamp.

## Time-on-air

`hal_lora_time_on_air()` validates the LoRa modulation and packet fields and
returns the rounded-up packet duration in milliseconds. Frequency and output
power limits are hardware-specific and are checked by
`hal_lora_radio_configure()`:

```c
uint32_t airtime_ms = 0;
status = hal_lora_time_on_air(&modem, 32u, &airtime_ms);
```

Use airtime when choosing an explicit TX timeout and when calculating legal
duty-cycle behavior.

## Concurrency and validation

Runtime calls serialize per handle. Lifecycle operations (`create` and
`destroy`) follow the library-wide single-core/single-owner rule and must run on
the core that owns the provider IRQ GPIOs. Packet buffers are copied before a
start call returns. Callback dispatch never holds the handle mutex.

Host coverage lives in `test_hal_lora_radio_lifecycle`,
`test_hal_lora_radio`, `test_hal_lora_sx127x`, `test_sx126x_adapter`,
`test_sx127x_adapter`, and the real-scheduler `test_lora_freertos_posix`. The
buildable
[`27_lora_point_to_point`](../../examples/27_lora_point_to_point/) project
targets RP2040 and STM32G474. The repeatable two-device procedure and serial
verifier are in
[`tests/hardware/lora_sx1262`](../../tests/hardware/lora_sx1262/).
