# 27 - LoRa point-to-point

Raw SX1262 ping/pong example covering blocking transmit, polling receive,
packet metadata, bounded receive timeouts and continuous receive. Build one
device as the initiator and the other with the `responder` variant.

The RP2040 configuration selects the integrated `rp2040-lora-lf` profile and
uses an explicit 434.0 MHz test configuration. This is intentionally not named
or exposed as a universal LF regulatory preset. The STM32G474 configuration
uses a separately wired Waveshare Core1262-HF and the EU868 technical preset.
Never attempt an over-the-air link between the LF and HF devices.

## Build

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 27_lora_point_to_point
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 27_lora_point_to_point
```

The dispatcher builds both the base initiator and `responder` variant. They can
also be built explicitly through `jh-vscode` with `--variant responder`.

## External Core1262-HF wiring

| Signal | RP family | STM32G474 |
|---|---|---|
| MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| CS | GP17 | PB0 |
| RESET / BUSY / DIO1 | GP20 / GP21 / GP22 | PB1 / PB2 / PB3 |
| RXEN / TXEN | GP10 / GP11 | PB4 / PB5 |

Supply the module and all I/O at 3.3 V, share ground, add local decoupling and
attach the correct HF antenna before transmitting. The driver uses 8 MHz SPI,
waits for BUSY, drives DIO3 for the TCXO and controls RXEN/TXEN independently.

The two normal build targets use different radio bands and therefore form two
separate hardware tests: two integrated LF boards, or two external HF modules
wired to an RP2040 and an STM32G474 host.
