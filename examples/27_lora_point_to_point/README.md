# 27 - LoRa point-to-point

Raw SX1262 ping/pong example covering DIO1-driven asynchronous transmit and
receive, callbacks, cancellation, packet metadata, bounded receive timeouts and
continuous receive. Build one device as the initiator and the other with the
`responder` variant. The no-transmit `probe` variant checks capabilities,
explicit calibration, current RSSI, CAD and standby.

The `link` and `link-responder` variants replace the raw ping/pong application
with `hal_lora_commands` over `hal_lora_link`. The initiator sends a binary
500-byte `echo` request to address `0x1002`. The responder dispatches it through
the shared command router and returns the exact bytes in a correlated response.
Both the request and response occupy three plaintext link fragments, so a
successful transaction covers command framing, request identifiers, route
dispatch, response correlation, fragmentation, reassembly, duplicate
suppression and bounded retransmission.

The transport-neutral `echo` route allows both `LORA_LINK` and `BLE_STREAM`
sources. This example attaches the LoRa adapter; the BLE command variant of
example 26 reuses the same route policy over authenticated BLE Stream.

When the selected board exposes a GPIO status LED, the LED remains on during
transmit and pulses for 120 ms after a packet is received. Boards without a
GPIO status LED keep the same radio behavior without visual signaling.

The default RP2040 and STM32G474 configurations select the fixed
`pico-core1262-hf` and `nucleo-g474re-core1262-hf` fixtures with Waveshare
Core1262-HF modules. Select `rp2040-lora-lf` explicitly for the integrated LF
board; it uses a deliberate 434.0 MHz test configuration, not a universal
regulatory preset. Never attempt an over-the-air link between LF and HF
devices.

## Build

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 27_lora_point_to_point
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 27_lora_point_to_point
```

The representative gate builds the base initiator plus `probe`, `responder`,
`link` and `link-responder`. Hardware-only variants `sf7` and
`responder-sf7` remain available through `jh-vscode`; they are excluded from
the representative compile gate.

Build only the command pair from the VS Code variant selector, or directly:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico-core1262-hf --variant link
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re-core1262-hf \
  --variant link-responder
```

For two integrated Waveshare LF boards, use `--target rp2040 --board
rp2040-lora-lf` with the `link` and `link-responder` variants. The complete
upload procedure, stable serial-port selection and `JHCMD1` acceptance criteria
are maintained in the
[central command-router hardware gate](../../doc/api/03_build_tests.md#sx1262-command-router-over-lora-hardware-gate).

The command example intentionally uses CRC-protected plaintext. Encrypted links
also require `HAL_ENABLE_CRYPTO`, a provisioned 32-byte secret and a session ID
that is never reused for the same address/key. See the
[reliable LoRa link API](../../doc/api/22_lora_link.md) before enabling AEAD.

## External Core1262-HF wiring

| Signal | RP family | STM32G474 |
|---|---|---|
| MISO / MOSI / SCK | GP16 / GP19 / GP18 | PB14 / PB15 / PB13 |
| CS | GP17 | PB0 |
| RESET / BUSY / DIO1 | GP20 / GP21 / GP22 | PB1 / PB2 / PB3 |
| RXEN / TXEN | GP10 / GP11 | PB4 / PB5 |

Supply the module and all I/O at 3.3 V, share ground, add local decoupling and
attach the correct HF antenna before transmitting. The driver uses 8 MHz SPI,
waits for BUSY, drives DIO3 for the TCXO and controls RXEN/TXEN independently.
On NUCLEO-G474RE, SPI2 deliberately avoids PA5 so the physically connected LD2
and public `HAL_LED_BUILTIN` remain usable. Do not hide a base-board device in
a composite profile as a way to reuse its still-connected pin.

The available hardware forms two separate tests: two integrated LF boards, or
two external HF modules wired to an RP2040 and an STM32G474 host.
