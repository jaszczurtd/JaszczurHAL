# Raw LoRa radio API

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

`hal_lora_radio` is the provider-neutral raw packet-radio facade. The current
provider integrates an SX1262 through the pinned official Semtech SX126x
driver and a shared adapter that uses only HAL SPI, GPIO, timing and mutex
services. The same implementation builds for RP2040, RP2350 and STM32G474;
the deterministic mock provides host tests.

The Stage 1 API uses blocking transmit and polling receive. Applications own
hardware and modem descriptors. Each opaque handle owns its TX/RX packet
buffers, state, mutex and diagnostics.

## Enable the module

Select the SX126x provider in `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_SX126X
```

The feature registry propagates `HAL_ENABLE_LORA` and `HAL_ENABLE_SPI`.
Selecting `HAL_ENABLE_LORA` alone is rejected because the facade requires one
provider.

The following tunables are available before `hal_config.h` is included:

| Macro | Default | Valid range | Purpose |
|---|---:|---:|---|
| `HAL_LORA_RADIO_MAX_INSTANCES` | 2 | 1..255 | Number of generation-tagged static handle slots |
| `HAL_LORA_SX126X_BUSY_TIMEOUT_MS` | 1000 | 1..60000 | Maximum wait for the SX126x BUSY line around a command |

## Hardware ownership

Initialize the selected SPI controller before creating a radio. The facade
serializes each command with the HAL bus lock and applies the device clock,
mode 0 and MSB-first settings for each transaction. It owns the radio CS,
RESET, BUSY, DIO1, RF-switch and optional DIO3 TCXO behavior described by the
hardware descriptor.

The SPI controller may be shared with other HAL devices. Destroying a radio
returns the SX1262 to standby and releases its handle without deinitializing
the shared bus.

## Integrated board configuration

`hal_lora_radio_config_from_board()` copies the active board profile's radio
facts. It returns `HAL_EUNSUPPORTED` when the selected board has no integrated
radio.

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

## External Waveshare Core1262-HF

`hal_lora_sx126x_core1262_hf_defaults()` fills the module-owned electrical
profile: dual RXEN/TXEN control, DCDC, DIO3-controlled 1.8 V TCXO, startup
delay, RF range, SPI limit and output-power limits. Host bus and pin assignments
remain application input. Assign RXEN to `rf_switch_pin_a` and TXEN to
`rf_switch_pin_b`; the helper installs the module's documented level table.

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
when the configured static pool is full.

Modem validation covers SX1262 hardware frequency/power limits, supported LoRa
bandwidths, spreading factors 5 through 12, coding rates 5 through 8, preamble,
header mode and implicit payload length.

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

## Polling receive

Start one bounded receive window and poll until a terminal result:

```c
status = hal_lora_radio_receive_start(radio, 1500u);
while (status == HAL_OK) {
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
packet. Call `hal_lora_radio_standby()` to stop continuous receive.

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

## State, power and diagnostics

`hal_lora_radio_get_state()` returns the stable public state. Stage 1 uses
`STANDBY`, `RX`, `TX`, `SLEEP` and `ERROR`; `CAD` is reserved for later radio
operations.

`hal_lora_radio_sleep()` enters the SX1262 warm-start sleep configuration.
`hal_lora_radio_standby()` wakes a sleeping radio or ends receive mode.

`hal_lora_radio_get_diagnostics()` copies counters for transmitted/received
packets, CRC errors, TX/RX timeouts, dropped packets, bus errors and resets,
plus the latest RSSI, SNR and error status.

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
`destroy`) follow the library-wide single-core/single-owner rule. The polling
API does not dispatch callbacks and does not retain caller packet buffers.

Host coverage lives in `test_hal_lora_radio_lifecycle`,
`test_hal_lora_radio`, and `test_sx126x_adapter`. The buildable
[`27_lora_point_to_point`](../../examples/27_lora_point_to_point/) project
targets RP2040 and STM32G474. The repeatable two-device procedure and serial
verifier are in
[`tests/hardware/lora_sx1262`](../../tests/hardware/lora_sx1262/).
