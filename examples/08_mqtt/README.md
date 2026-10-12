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
  --project examples/08_mqtt --assembly picow
```

`device_tree.json` describes the hardware of every assembly: `picow`
(Pico W, the default), `pico2w` (Pico 2 W), `esp32s3-zero` (ESP32-S3-Zero
with its own WiFi) and `nucleo-pim730`. `nucleo-pim730` is a plain
NUCLEO-G474RE with an external PIM730/RM2 module, without a separate board
profile; the table below repeats the module wiring. The project does not
include RP2350 RISC-V.

| PIM730/RM2 signal | NUCLEO-G474RE |
|---|---|
| CS | PB12, CN10 pin 16 |
| DAT | PB15, CN10 pin 26 |
| WL_ON | PB14, CN10 pin 28 |
| CLK | PB13, CN10 pin 30 |
