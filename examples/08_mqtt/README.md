# 08 - Publishing and receiving MQTT messages

This example connects to WiFi and an MQTT broker. Every five seconds, it
publishes JSON containing a counter and RSSI to
`jaszczurhal/example/telemetry`. It also subscribes to
`jaszczurhal/example/cmd` and prints incoming messages to the console.

While disconnected, WiFi or broker connection attempts are spaced at least
five seconds apart. Once connected, the application loop regularly calls
`hal_mqtt_loop()`.

## Configuration

Set `WIFI_SSID` and `WIFI_PASSWORD` in `app.c`. Also review `MQTT_HOST`,
`MQTT_PORT`, `MQTT_CLIENT_ID`, `MQTT_TOPIC_PUB`, and `MQTT_TOPIC_SUB`.
The defaults select `broker.hivemq.com` on port `1883`. This code configures
neither TLS nor MQTT client authentication, hovewer that functionality is
also supported.

Console output is truncated to 95 payload bytes to fit the diagnostic buffer;
this does not describe an API packet-size limit. `HAL_ENABLE_MQTT` enables
the MQTT module.

## Build

Run from the repository root:

```bash
vscode/entry/jh-vscode build \
  --project examples/08_mqtt --target rp2040 --board picow
```

The default boards are `picow` for RP2040, `pico2w` for RP2350 ARM, and
`nucleo-g474re-pim730` for STM32G474 with an external PIM730/RM2 module.
The project does not include RP2350 RISC-V.
