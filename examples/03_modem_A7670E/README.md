# 03 - MQTT through a SIMCom A7670 modem

This example connects a modem to a cellular network, opens an MQTT connection
with TLS, and sends and receives messages. It uses SIMCom A76xx AT-command
support, enabled by `HAL_ENABLE_A7670`.

At startup, it waits for the SIM and network registration, configures the APN,
and subscribes to `dpf/cmd`. Every 10 seconds, it attempts to publish
`{"hello":"world"}` to `dpf/data`. Receiving `modem_reset` on the command
topic requests a modem power toggle and connection reinitialization.

## Modem setup

Set `APN`, the broker address and port, client identifier, credentials, and
MQTT topics in `app.c`. `SSL_CA_CERT` names `ca.pem`; the application does
not upload that file to the modem. Prepare the certificate required by its
TLS configuration.

The example sets `ignore_local_time = true` and `enable_sni = false`.
These are not a ready-to-use TLS configuration for every broker. Check the
requirements of the server and modem you use.

| Signal on the RP microcontroller | Pin / setting |
|---|---|
| UART TX, to modem RX | GP4 |
| UART RX, from modem TX | GP5 |
| Modem power-control signal | GP6 |
| Serial port | `HAL_UART_PORT_2`, 115200 baud, 8N1 |

Use the power supply and logic levels specified for your module. The code
uses a 1500 ms control pulse and waits 15 s after toggling power. Check the
hardware requirements before changing these values.

## Build

Run from the repository root:

```bash
vscode/entry/jh-vscode build \
  --project examples/03_modem_A7670E --target rp2040 --board pico
```

The project includes RP2040, RP2350 ARM, and RP2350 RISC-V configurations.
Modem and MQTT startup errors are reported to the console;
the example does not implement recovery from every possible failure.
