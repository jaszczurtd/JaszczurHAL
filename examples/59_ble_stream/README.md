# 59_ble_stream

JH BLE Stream v1 consumer: a connectable Peripheral that publishes the stream
service and exchanges payloads only inside a mutually authenticated session.

The device advertises as `JH Stream`, serves the protocol version and the
capability bitmask to any client, and refuses payload traffic until the client
proves knowledge of the per-device secret. Once authenticated it publishes a
telemetry line every second and logs whatever the client sends.

## Build and run

```bash
./scripts/examples_dispatcher.py build --target rp2040 --example 59_ble_stream
./scripts/examples_dispatcher.py build --target stm32g474 --example 59_ble_stream
```

Supported profiles are RP2040 `picow` and STM32G474 `nucleo-g474re-pim730`.

## Provisioning the secret

`kDeviceSecret` in [`app.cpp`](app.cpp) stands in for provisioning so the
example builds and runs as is. A product replaces it with a per-device value of
at least 256 bits, delivered to the client out of band - for example through a
label QR code or an authenticated USB channel - and never shares one secret
across devices.

`hal_ble_stream_set_secret()` installs it, `hal_ble_stream_clear_secret()`
implements factory reset, and installing a new secret invalidates any session
built on the previous one.

## Client side

A client completes the handshake by sending `HELLO`, verifying the device proof
in `HELLO_ACK`, and answering with `AUTH`. Both proofs and the two directional
keys come from HMAC-SHA256 over a transcript covering the profile name, the
protocol version, both capability sets, the session identifier and both nonces.
`DATA` frames use ChaCha20-Poly1305 with a directional counter. The frame layout
and every constant live in
[`hal_ble_stream.h`](../../src/hal/hal_ble_stream.h).

The negotiated ATT MTU must reach `HAL_BLE_STREAM_MIN_ATT_MTU` before a
handshake fits in one write; the example logs the MTU it observes.

## What the example shows

- publishing the service with a capability set;
- refusing payload traffic without a session;
- draining received payloads with explicit overflow reporting;
- handling `HAL_EAGAIN` backpressure on send;
- restarting advertising after a disconnect.

For an independent client implementation and dual-target negative test, see
the [`bluetooth_stream` hardware gate](../../tests/hardware/bluetooth_stream/).
