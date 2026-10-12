# 09 - Connecting through WireGuard

This example connects to WiFi, configures a WireGuard tunnel, and checks the
peer connection every five seconds. If the peer is not up, it calls
`hal_wireguard_kick_handshake_text()` with a probe address to initiate a
handshake.

## Configuration

Set the WiFi credentials and tunnel parameters in `app.c`: the local IP
address, device private key, peer address and port, peer public key, and the
network reachable through the tunnel. These are the `WIFI_*` and `WG_*`
constants. `WG_PROBE_IP` and `WG_PROBE_PORT` select the destination used to
initiate probe traffic.

`base64-private-key`, `base64-peer-public-key`, and `vpn.example.com` are
placeholders, not a working configuration. Configure the remote peer as well.
`HAL_ENABLE_WIREGUARD` enables WireGuard support.

`tunnel started` reports successful tunnel initialization, not an established
peer connection. The application checks the latter separately through
`hal_wireguard_peer_up()`.

## Build

Run from the repository root:

```bash
vscode/entry/jh-vscode build \
  --project examples/09_wireguard --assembly picow
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
