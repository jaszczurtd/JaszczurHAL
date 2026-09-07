<a id="27---lora-point-to-point"></a>

# 27 - Exchanging packets and commands over LoRa

This example connects two devices with SX1262 radios. In the base application,
one sends a packet and the other replies; build an initiator and a
`responder` variant. Transmission and reception are asynchronous, using DIO1
and callbacks. The code also demonstrates cancellation, packet metadata,
receive timeouts, and continuous reception.

The `probe` variant does not transmit. It checks radio capabilities,
calibration, current RSSI, channel activity detection (CAD), and standby.

The `link` and `link-responder` variants replace simple ping/pong with
commands carried by `hal_lora_commands` and `hal_lora_link`. The initiator
sends a 500-byte binary `echo` request to address `0x1002`. The responder
runs the command through the shared router and returns the same bytes in a
response associated with that request.

The request and response each use three plaintext fragments. The exchange
checks message framing, request identifiers, handler selection, response
matching, fragmentation and reassembly, duplicate suppression, and bounded
retries.

The `echo` route allows `LORA_LINK` and `BLE_STREAM` sources. This project
starts only the LoRa transport; the command variant of example 26 uses the
same rule with authenticated BLE Stream.

If the board profile provides a GPIO status LED, it stays on during
transmission and lights for 120 ms after a packet arrives. Radio behavior
is unchanged on boards without that LED.

The defaults are `pico-core1262-hf` and `nucleo-g474re-core1262-hf` with
Waveshare Core1262-HF modules. Select `rp2040-lora-lf` for the integrated
LF board. Its 434.0 MHz frequency is a test setting, not a declaration of
regulatory compliance in every region. Do not pair LF and HF devices over
the radio link.

## Build

Run from the repository root:

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 27_lora_point_to_point
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 27_lora_point_to_point
```

The default compile checks cover the initiator, `probe`, `responder`, `link`,
and `link-responder`. The hardware-test variants `sf7` and `responder-sf7`
can be built through `jh-vscode` but are not part of that check set.

To build only the command pair, choose the variants in VS Code or run:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico-core1262-hf --variant link
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re-core1262-hf \
  --variant link-responder
```

For two integrated Waveshare LF boards, select
`--target rp2040 --board rp2040-lora-lf` with `link` and `link-responder`.
See the
[LoRa command hardware tests](../../doc/api/en/03_build_tests.md#sx1262-command-router-over-lora-hardware-gate)
for uploading, stable serial-port selection, and `JHCMD1` acceptance criteria.

**Commands in this example are not encrypted.** They have CRC protection,
which does not provide authentication. An encrypted link also requires
`HAL_ENABLE_CRYPTO`, a provisioned 32-byte secret, and a session identifier
that is never reused with the same address and key. Read the
[`hal_lora_link` API](../../doc/api/en/22_lora_link.md) before enabling AEAD.

## External Core1262-HF wiring

| Signal | RP family | STM32G474 |
|---|---|---|
| MISO / MOSI / SCK | GP16 / GP19 / GP18 | PB14 / PB15 / PB13 |
| CS | GP17 | PB0 |
| RESET / BUSY / DIO1 | GP20 / GP21 / GP22 | PB1 / PB2 / PB3 |
| RXEN / TXEN | GP10 / GP11 | PB4 / PB5 |

Use 3.3 V power and logic levels, connect grounds, add local decoupling,
and attach the correct HF antenna before transmitting. The driver uses
8 MHz SPI, waits for BUSY to clear, drives the TCXO through DIO3, and
controls RXEN and TXEN separately.

NUCLEO-G474RE uses SPI2 to leave PA5 available. PA5 remains physically
connected to LD2 and exposed as `HAL_LED_BUILTIN`. A composite board profile
must not hide that LED or describe its pin as unconnected.

The available wiring supports two separate test setups: two integrated LF
boards, or two external HF modules connected to RP2040 and STM32G474 hosts.
