# 27 - LoRa point-to-point

Raw SX1262 ping/pong example covering DIO1-driven asynchronous transmit and
receive, callbacks, cancellation, packet metadata, bounded receive timeouts and
continuous receive. Build one device as the initiator and the other with the
`responder` variant. The no-transmit `probe` variant checks capabilities,
explicit calibration, current RSSI, CAD and standby.

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

The representative gate builds the base initiator plus `probe` and `responder`.
Hardware-only variants `sf7` and `responder-sf7` remain available through
`jh-vscode`; they are excluded from the representative compile gate.

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
