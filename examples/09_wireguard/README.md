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
  --project examples/09_wireguard --target rp2040 --board picow
```

The default boards are `picow` for RP2040, `pico2w` for RP2350 ARM, and
`nucleo-g474re-pim730` for STM32G474 with a PIM730/RM2 module.
The project does not include RP2350 RISC-V.
