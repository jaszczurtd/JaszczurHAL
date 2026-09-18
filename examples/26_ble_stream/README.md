<a id="26---ble-stream"></a>

# 26 - Exchanging data and commands over BLE

This example exposes JH BLE Stream v1 and exchanges data with a client after
mutual authentication. The board acts as a BLE Peripheral: it advertises and
accepts a connection from a Central.

The base application advertises as `JH Stream`. Any client can read the
protocol version and capability bitmask, but application data is available
only after the client proves knowledge of the device secret. Once
authenticated, the application sends a telemetry line every second and prints
received data to the console. If transmission is temporarily unavailable,
it retains at most one sample for retry.

The `commands` and `commands-freertos` variants advertise as `JH Commands`.
They exchange binary command requests, responses, and events with an
authenticated client. `hal_ble_commands` has exclusive access to Stream
payloads, while command handlers remain independent of the transport.

## Build and run

Run from the repository root:

```bash
./scripts/examples_dispatcher.py build --target rp2040 --example 26_ble_stream
./scripts/examples_dispatcher.py build --target rp2350-arm --example 26_ble_stream
./scripts/examples_dispatcher.py build --target stm32g474 --example 26_ble_stream
```

For this project, the dispatcher builds the base firmware and both command
variants. To build only one variant, use:

```bash
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands
vscode/entry/jh-vscode build --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands-freertos
```

The default boards are `picow` for RP2040, `pico2w` for RP2350 ARM, and
`nucleo-g474re-pim730` for STM32G474. RP2040 also has an explicit `pico-rm2`
build configuration, but its separate hardware test remains pending:

```bash
vscode/entry/jh-vscode build \
  --project examples/26_ble_stream \
  --target rp2040 --board pico-rm2
```

RP2350 RISC-V is unsupported because CYW43 Bluetooth transport is not enabled
for it.

CYW43/BLE initialization runs on the first `app_task0()` call. With FreeRTOS,
this places it after scheduler startup. The configuration reserves a
1024-word task stack in that case; the default 512-word stack was not enough
for authenticated session setup on RP hardware.

## Provisioning the secret

`kDeviceSecret` in [`app.c`](app.c) is an example value. In a deployed
device, replace it with a device-specific secret of at least 256 bits.
Give it to the client through a separate channel, such as a label QR code
or an authenticated USB connection. Do not share one secret across devices.

`hal_ble_stream_set_secret()` sets the secret, and
`hal_ble_stream_clear_secret()` removes it, for example during a factory
reset. Installing a new secret invalidates a session based on the previous one.

## Client side

The client sends `HELLO`, verifies the device's proof of secret knowledge
in `HELLO_ACK`, and responds with `AUTH`. Both proofs and the separate keys
for each direction are derived using HMAC-SHA256. The input covers the
profile name, protocol version, both capability sets, session identifier,
and both nonces. `DATA` frames use ChaCha20-Poly1305 and a separate counter
for each direction. Frame layouts and constants are defined in
[`hal_ble_stream.h`](../../src/hal/bluetooth/hal_ble_stream.h).

ATT MTU must reach `HAL_BLE_STREAM_MIN_ATT_MTU` before session setup so the
handshake messages fit the required write size. The example logs the
negotiated MTU.

The command variants use Linux/BlueZ as the Central. The documented
JaszczurHAL integration provides Peripheral and passive Observer roles;
a second board running this example is another Peripheral, not a client.
The hardware verifier performs the client handshake and splits command data
according to the MTU:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 --board picow --runtime baremetal
```

It tests a fragmented 500-byte binary `echo`, handler and security metadata,
source restrictions, unknown commands, an outbound event, a
Peripheral-originated request, and one reconnect. Use `--runtime freertos`
with the `commands-freertos` image.

## What the example shows

The base application initializes the BLE controller, reads its address,
publishes the service, and handles connection events. It drains incoming data,
reports overflow, and rejects application data outside a session.
After `HAL_EAGAIN`, it retries one retained telemetry sample. It also keeps
an advertising request active so advertising resumes after a disconnect.

The command variants register source restrictions in the router, process
messages incrementally, handle responses, and let the Peripheral send its
own event and request to the Central. One command adapter is attached to
the already initialized Stream.

See the
[`bluetooth_stream` hardware tests](../../tests/hardware/bluetooth_stream/README.md)
for an independent client and multi-target stability and security checks.
