# Bluetooth Low Energy Peripheral and Observer API

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

The `hal_ble` module is an opt-in Bluetooth Low Energy Peripheral and passive
Observer API. Define `HAL_ENABLE_BLE` and include `hal/bluetooth/hal_ble.h` or
the umbrella `JaszczurHAL.h` header.

The current release provides one Peripheral connection, connectable legacy
advertising, passive legacy scanning, copied advertising reports, AD structure
parsing, controller and connection events, ATT MTU reporting, and a static
GATT database containing the mandatory GAP and GATT services. It does not yet
provide active scanning or scan-response requests, arbitrary application
characteristics, a GATT client, pairing, or bonding. The opt-in
`HAL_ENABLE_BLE_STREAM` profile adds one fixed authenticated application
service and its notification path.

## Supported profiles

| Target | Board | Radio | Validation |
|---|---|---|---|
| `rp2040` | `picow` | onboard CYW43439 | Observer and bare-metal/FreeRTOS Stream hardware gates passed |
| `rp2350-arm` | `pico2w` | onboard CYW43439 | Observer, bare-metal/FreeRTOS Stream, and active Stream+WiFi/MQTT coexistence gates passed |
| `rp2040` | `pico-rm2` | external PIM730/RM2 CYW43439 over PIO | build-supported; dedicated hardware gate pending |
| `stm32g474` | `nucleo-g474re-pim730` | external PIM730/RM2 CYW43439 over gSPI | Peripheral and Observer gates plus full bare-metal/FreeRTOS Stream display-load gates, including IWDG reset, passed |
| `mock` | `host-mock` | deterministic test backend | host tests |

The RP2350 backend supports Pico 2 W only with the `rp2350-arm` target. Pico 2
W with `rp2350-riscv` is unsupported because the CYW43 Bluetooth transport is
not enabled for that target. `HAL_ENABLE_BLE` is rejected at compile time on
unsupported targets and on profiles without a Bluetooth controller. Runtime
capability checks use
`HAL_BOARD_CAP_BLUETOOTH_CONTROLLER` and additionally require
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND` for an external module.

The 2026-08-25 Linux/BlueZ Stream runs passed 50 authenticated reconnects,
five minutes of approximately 10 messages per second, lifecycle restart,
queue saturation/recovery, and the negative security cases on Pico W
bare-metal and Pico 2 W bare-metal/FreeRTOS. An unattended watchdog reset
changed the reported reset reason to watchdog, changed a random per-boot
identifier, retained the controller address, and required a new authenticated
session. The same watchdog-reset oracle passed on STM32G474 with PIM730 and an
ILI9341 load in both bare-metal and FreeRTOS builds, retaining address
`28:CD:C1:19:18:19` across each reset. This is an MCU-reset interruption test,
not a physical removal of VBUS. The corrected Pico W FreeRTOS fixture passed
the complete gate after the Stream backend began requesting a 15 ms connection
interval with zero peripheral latency: 50/50 reconnects, 3000 authenticated
messages in 300.0 seconds (10.00 Hz), saturation, and all negative security
and recovery cases. A central may reject the request without terminating the
connection. Native Windows host validation and downstream consumer/lights-
timer integration are deferred and do not block the recorded passing BLE
hardware results.

Passive Observer validation on the same date passed on Pico W and Pico 2 W:
both entered `HAL_BLE_STATE_SCANNING`, retained seven reports including one
Teltonika and three Eddystone signatures, and reported zero queue drops.

## Lifecycle and polling

```cpp
#include <JaszczurHAL.h>

#include <cstring>

static hal_ble_advertising_handle_t advertising;
static hal_status_t ble_status = HAL_NONE;
static bool ble_started;

static void on_ble_event(const hal_ble_event_t *event, void *) {
  if (event->type == HAL_BLE_EVENT_CONTROLLER_READY) {
    static const uint8_t payload[] = {
        0x02, 0x01, 0x06,                   // general-discoverable flags
        0x07, 0x09, 'J', 'H', ' ', 'B', 'L', 'E'}; // complete name
    hal_ble_advertising_config_t config{};
    config.interval_min = 0x00a0; // 100 ms
    config.interval_max = 0x00a0;
    config.data_length = static_cast<uint8_t>(sizeof(payload));
    std::memcpy(config.data, payload, sizeof(payload));
    (void)hal_ble_advertising_start(&config, &advertising);
  }
}

static hal_status_t start_ble(void) {
  hal_status_t status = hal_ble_initialize();
  if (status == HAL_OK) {
    status = hal_ble_set_event_callback(on_ble_event, nullptr);
  }
  return status;
}

extern "C" void app_task0(void) {
  if (!ble_started) {
    ble_started = true;
    ble_status = start_ble();
  }
  if (ble_status != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }
  const hal_status_t poll_status = hal_ble_poll();
  if (poll_status != HAL_OK && poll_status != HAL_EOVERFLOW) {
    /* Record or recover from the controller error. */
  }
  hal_delay_ms(1u);
}
```

`hal_ble_initialize()` starts the shared CYW43 radio owner and returns when
startup has been accepted. Readiness is asynchronous and is reported by
`HAL_BLE_EVENT_CONTROLLER_READY`. Initialization and deinitialization are
idempotent after success. Deinitialization invalidates every connection and
advertising handle, clears the event queue, and removes the callback.

Call `hal_ble_poll()` frequently from one task or cooperative loop. It services
the controller and then dispatches callbacks outside the shared WiFi/Bluetooth
radio lock. Calling `hal_ble_poll()`, changing the callback, or deinitializing
recursively from a callback returns `HAL_EBUSY`; read-only queries are allowed.

## Events

`HAL_BLE_EVENT_QUEUE_DEPTH` configures the copied, bounded event queue and
defaults to 8. The public events are:

- `HAL_BLE_EVENT_CONTROLLER_READY`;
- `HAL_BLE_EVENT_ADVERTISING_STARTED` and
  `HAL_BLE_EVENT_ADVERTISING_STOPPED`;
- `HAL_BLE_EVENT_CONNECTED` and `HAL_BLE_EVENT_DISCONNECTED`;
- `HAL_BLE_EVENT_MTU_UPDATED`;
- `HAL_BLE_EVENT_SCAN_STARTED`, `HAL_BLE_EVENT_SCAN_STOPPED`, and
  `HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE`;
- `HAL_BLE_EVENT_ERROR`.

Choose one consumption model: register a callback drained by `hal_ble_poll()`,
or pop events with `hal_ble_event_next()`. Both consume the same queue.
`hal_ble_event_next()` returns `HAL_EAGAIN` when empty. If the queue fills, new
events are dropped, `hal_ble_info_t::dropped_events` increases, and the next
poll reports `HAL_EOVERFLOW` without stopping BLE.

The ready event has no peer address; call `hal_ble_get_local_address()` after
it. A connected event contains the peer address and a new opaque connection
handle. MTU and disconnected events refer to that same handle.

## Advertising

`hal_ble_advertising_start()` copies the complete configuration before
returning. The legacy payload must contain 1 to 31 bytes. The minimum interval
must be between `0x0020` and `0x4000` units (20 ms to 10.24 s), and the maximum
must be at least the minimum and no greater than `0x4000`.

`HAL_OK` means the request was accepted. Wait for
`HAL_BLE_EVENT_ADVERTISING_STARTED` for completion. Advertising requested
before controller readiness starts when the controller becomes ready. A
successful connection pauses it, and a disconnect restarts it while the
request remains active. Stop the request with its opaque advertising handle.
Do not submit a second start request after `HAL_BLE_EVENT_DISCONNECTED`; the
original request already owns the automatic restart.

## Passive Observer scanning

`hal_ble_scan_start()` accepts an interval, a window, and an optional duplicate
filter. Both timing values use 0.625 ms Bluetooth units and must be between
`HAL_BLE_SCAN_INTERVAL_MIN` and `HAL_BLE_SCAN_INTERVAL_MAX`; the window must
not exceed the interval. `HAL_OK` means the request was accepted. Wait for
`HAL_BLE_EVENT_SCAN_STARTED` for completion.

Scanning is passive and receives legacy advertising packets only. It does not
transmit scan requests, initiate connections, pair, or expose a GATT client.
The initial Observer release also keeps scanning mutually exclusive with
advertising and a Peripheral connection; conflicting starts return
`HAL_EBUSY`.

Reports are copied into a separate fixed-size queue configured by
`HAL_BLE_SCAN_REPORT_QUEUE_DEPTH`, which defaults to 8. A
`HAL_BLE_EVENT_SCAN_REPORT_AVAILABLE` event means at least one report can be
read with `hal_ble_scan_report_next()`. Drain all available reports after that
event. The call returns `HAL_EAGAIN` when the queue is empty. If reports were
dropped, it first returns `HAL_EOVERFLOW` to acknowledge the loss; call it
again to read the oldest retained report. The cumulative and pending counts
are available through `hal_ble_info_t`.

Each report contains a copied address, RSSI, legacy event type, and up to 31
payload bytes. `hal_ble_advertising_field_next()` iterates the length-prefixed
AD structures without allocation. Start with an offset of zero; `HAL_EAGAIN`
marks the end and `HAL_EIO` rejects malformed input. The returned field data
points into the report and remains valid while that report object exists.

### Observer example

The following loop starts a 60 ms/30 ms passive scan and drains every retained
report. Replace `consume_ad_field()` with application-specific handling for AD
types such as complete local name (`0x09`) or manufacturer data (`0xff`).

```cpp
static void consume_ad_field(const hal_ble_advertising_report_t &report,
                             const hal_ble_advertising_field_t &field);

static hal_status_t observer_status = HAL_NONE;
static bool observer_started;

static void on_observer_event(const hal_ble_event_t *event, void *) {
  if (event->type == HAL_BLE_EVENT_CONTROLLER_READY) {
    hal_ble_scan_config_t scan{};
    scan.interval = 0x0060; // 60 ms
    scan.window = 0x0030;   // 30 ms
    scan.filter_duplicates = true;
    (void)hal_ble_scan_start(&scan);
  }
}

static void drain_scan_reports(void) {
  for (;;) {
    hal_ble_advertising_report_t report{};
    const hal_status_t status = hal_ble_scan_report_next(&report);
    if (status == HAL_EOVERFLOW) {
      continue; // Loss acknowledged; retained reports are still available.
    }
    if (status == HAL_EAGAIN) {
      return;
    }
    if (status != HAL_OK) {
      return;
    }

    size_t offset = 0;
    hal_ble_advertising_field_t field{};
    while (hal_ble_advertising_field_next(&report, &offset, &field) ==
           HAL_OK) {
      consume_ad_field(report, field);
    }
  }
}

static hal_status_t start_observer(void) {
  hal_status_t status = hal_ble_initialize();
  if (status == HAL_OK) {
    status = hal_ble_set_event_callback(on_observer_event, nullptr);
  }
  return status;
}

extern "C" void app_task0(void) {
  if (!observer_started) {
    observer_started = true;
    observer_status = start_observer();
  }
  if (observer_status != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }
  (void)hal_ble_poll();
  drain_scan_reports();
  hal_delay_ms(1u);
}
```

## Connections and MTU

Only one Peripheral connection is supported. Connection and advertising
handles are nonzero, opaque, and invalid after their terminal event,
deinitialization, or a controller failure. Passing a stale handle returns
`HAL_ENOENT`.

`hal_ble_disconnect()` queues a local disconnect. Completion arrives as
`HAL_BLE_EVENT_DISCONNECTED`. `hal_ble_get_mtu()` returns 23 until BTstack
reports a negotiated value through `HAL_BLE_EVENT_MTU_UPDATED`.

## Status and failure model

The API uses `hal_status_t` throughout. Common results are:

| Status | Meaning |
|---|---|
| `HAL_OK` | synchronous query succeeded or asynchronous command was accepted |
| `HAL_EUNINIT` | BLE has not been initialized |
| `HAL_EAGAIN` | readiness/event data is not available yet |
| `HAL_EBUSY` | conflicting request or callback/poll recursion |
| `HAL_ENOENT` | stale or unknown opaque handle |
| `HAL_EOVERFLOW` | a bounded event or scan-report queue dropped data |
| `HAL_EUNSUPPORTED` | selected board has no required radio hardware |
| `HAL_EHW` / `HAL_EIO` | controller or transport failure |

Use `hal_ble_get_info()` for a consistent state snapshot, current handles,
generation, last status, MTU, scan state, pending report count, and both drop
counters. A fatal controller or transport error moves the subsystem to
`HAL_BLE_STATE_FAILED`, invalidates its handles, stops scanning, and advances
the generation.

## JH BLE Stream v1

`HAL_ENABLE_BLE_STREAM` adds `hal_ble_stream.h`, a bounded byte stream carried
by one static GATT service. The flag enables `HAL_ENABLE_BLE` and
`HAL_ENABLE_CRYPTO`.

The header is the single source of truth for the service UUIDs, the frame
layout and the capability bits. Changing any of them raises the profile
version.

| Element | UUID |
|---|---|
| Service | `B7CE0001-3C13-4FE2-801F-D71BDAB1369B` |
| RX (write, write-without-response) | `B7CE0002-3C13-4FE2-801F-D71BDAB1369B` |
| TX (notify) | `B7CE0003-3C13-4FE2-801F-D71BDAB1369B` |
| Protocol version (read) | `B7CE0004-3C13-4FE2-801F-D71BDAB1369B` |
| Capabilities (read) | `B7CE0005-3C13-4FE2-801F-D71BDAB1369B` |

### Security model

A client without a session reads the protocol version, the capability bitmask
and nothing else. Every payload exchange requires a mutually authenticated
session over a per-device secret of at least 256 bits, delivered out of band.

The handshake binds a transcript built from the profile name, the protocol
version, both capability sets, a session identifier and two random nonces.
Four separate HMAC-SHA256 domains produce the device proof, the client proof
and two directional session keys. `DATA` frames are protected with
ChaCha20-Poly1305; the direction and a strictly increasing counter enter both
the nonce and the associated data. Counters are consecutive: the receiver
accepts exactly the previous value plus one, so a replay, decrease, or forward
gap closes the session.

Sessions fail closed. A wrong proof, a forged tag, a replayed or decreasing
counter, a counter about to wrap, an entropy failure, a disconnect, a
controller generation change, unsubscription and the idle timeout all drop the
session and zero its directional keys. Repeated authentication failures move
the profile into a bounded backoff window during which handshakes are refused.
Rotating or clearing the secret invalidates any session built on the previous
one.

BLE Stream shares the target-neutral `jh_secure_random_bytes()`,
`jh_secure_zeroize()` and `jh_constant_time_compare()` primitives with Serial
Session. Proof, nonce, transcript, directional-key and queued plaintext
buffers are erased on their terminal paths; no BLE-local zeroize or tag
comparison implementation exists.

The device address and link-layer pairing are not authorization. `Just Works`
encrypts the link without MITM protection, which is why product operations
depend on the application session rather than on the BLE link alone.

### ATT MTU

One frame travels in a single write or notification. The handshake needs at
least `HAL_BLE_STREAM_MIN_ATT_MTU`, and a full-size payload needs
`HAL_BLE_STREAM_FULL_PAYLOAD_ATT_MTU`. Watch `HAL_BLE_EVENT_MTU_UPDATED` and
keep payloads within what the negotiated MTU carries. A send that cannot fit
the current MTU returns `HAL_EOVERFLOW` without closing the authenticated
session.

Handshake responses and application payloads use bounded pending slots. An
`HAL_EAGAIN` from the controller retains the frame and does not consume its
directional counter; the next poll or can-send event retries it. The BTstack
notification itself is issued only by the shared CYW43 radio service while it
owns the radio lock.

### Stream example

Initialize the BLE subsystem from the first application-task iteration,
install a unique provisioned secret, and then service both layers from that
same task. On FreeRTOS targets `app_start()` runs before the scheduler and must
not start CYW43. Advertising uses the Peripheral flow shown above; the fixed
Stream service appears in its GATT database automatically. A Stream task needs
at least the hardware-validated 1024-word stack budget used by the example and
hardware fixture.

```c
hal_status_t start_stream(const uint8_t *device_secret, size_t secret_length) {
  hal_status_t status = hal_ble_initialize();
  if (status != HAL_OK) {
    return status;
  }

  hal_ble_stream_config_t config = {0};
  config.capabilities =
      HAL_BLE_STREAM_CAP_TELEMETRY | HAL_BLE_STREAM_CAP_DIAGNOSTICS;
  status = hal_ble_stream_initialize(&config);
  if (status != HAL_OK) {
    return status;
  }
  return hal_ble_stream_set_secret(device_secret, secret_length);
}

static uint8_t echo_payload[HAL_BLE_STREAM_MAX_PAYLOAD];
static size_t echo_length;
static bool echo_pending;

static hal_status_t try_send_echo(void) {
  const hal_status_t status =
      hal_ble_stream_send(echo_payload, echo_length);
  if (status != HAL_EAGAIN) {
    echo_pending = false;
    echo_length = 0u;
  }
  return status;
}

void service_stream(void) {
  const hal_status_t poll_status = hal_ble_poll();
  if (poll_status != HAL_OK && poll_status != HAL_EOVERFLOW) {
    echo_pending = false;
    echo_length = 0u;
    return;
  }

  if (echo_pending) {
    const hal_status_t sent = try_send_echo();
    if (sent != HAL_OK) {
      /* HAL_EAGAIN keeps exactly one pending echo; other errors discard it. */
      return;
    }
  }

  for (;;) {
    const hal_status_t received =
        hal_ble_stream_receive(echo_payload, sizeof(echo_payload), &echo_length);
    if (received == HAL_EOVERFLOW) {
      continue; /* Loss acknowledged; drain the retained queue. */
    }
    if (received != HAL_OK) {
      echo_length = 0u;
      break;
    }

    const hal_status_t sent = try_send_echo();
    if (sent == HAL_EAGAIN) {
      echo_pending = true;
      return; /* Retry this echo before receiving another payload. */
    } else if (sent == HAL_EOVERFLOW) {
      /* The payload does not fit the negotiated ATT MTU. */
      return;
    } else if (sent == HAL_EAUTH) {
      /* The session closed; the client must authenticate again. */
      return;
    } else if (sent != HAL_OK) {
      return;
    }
  }
}
```

The example retains at most one echo after `HAL_EAGAIN` and retries it before
removing another RX payload. A disconnect or any other send error discards that
pending echo so data from an old session cannot enter a new one.

`hal_ble_stream_get_info()` reports the state, negotiated capabilities,
directional counters, authentication failures, replay rejections and queue
depth for diagnostics.

## WiFi coexistence and ownership

BLE and WiFi share one CYW43 controller, transport, radio runtime, and service
lock. Applications must not link Pico SDK `pico_cyw43_arch` or
`pico_btstack_cyw43` alongside this backend. BLE callbacks are deferred until
after radio servicing, so application code never runs under that lock.

The 2026-08-25 Pico 2 W active coexistence gate kept an authenticated Stream
connection active while MQTT traffic forced a WiFi disconnect and reconnect.
Both bare-metal and FreeRTOS sustained 10.00 BLE echoes/s for more than 607 s
with zero loss. Bare-metal completed 6079/6079 echoes (94.7 ms mean, 249.0 ms
maximum latency); FreeRTOS completed 6077/6077 (93.7 ms mean, 204.1 ms
maximum). Each run carried 34 BLE echoes through the WiFi reconnect window,
re-established WiFi and MQTT, retained both radio-runtime references, and
reported no BLE, Stream, MQTT, HCI, queue, or event errors. The measured
maximum BLE poll time was 4.768 ms bare-metal and 5.618 ms FreeRTOS. The final
FreeRTOS rerun used the strengthened MQTT-progress oracle: its observation
delta was 5794 echoes at 9.66 Hz, with zero stagnant one-second summaries.

## License and distribution boundary

BLE firmware links BlueKitchen BTstack from the exact revision recorded in
`third_party/btstack_version.conf`. Two distinct license texts are tracked:

- the standard BlueKitchen
  [`third_party/LICENSE.BTstack`](../../third_party/LICENSE.BTstack) grant
  permits redistribution, use, and modification only for personal benefit and
  not for commercial purpose or monetary gain. Its source and binary
  redistribution conditions require the copyright notice, conditions, and
  disclaimer to be retained or reproduced as specified in that text;
- the separate Raspberry Pi
  [`src/hal/bluetooth/LICENSE.RP`](../../src/hal/bluetooth/LICENSE.RP) grant
  applies to a `Customer`, defined as a purchaser of a listed `Product`. It
  permits that Customer to use, modify, integrate, and distribute BTstack only
  with the defined `Products` or `Customer Products`. The listed Products are
  Pico W, Pico WH, Pico 2 W, Pico 2 WH, and RM2; Customer Products are products
  manufactured or distributed by Customers which use or are derived from
  those Products. This is a product-scoped grant, not a general permission for
  every board or device containing a CYW43 controller.

The applicable grant depends on the physical product and its distribution.
Review the complete tracked license texts and satisfy the conditions of the
grant relied upon; uses outside them may require a separate BlueKitchen
license. This section is a technical inventory and is not legal advice. These
conditions apply to BLE-enabled artifacts, not to JaszczurHAL builds that do
not compile BTstack.

See the buildable [`26_ble_stream` example](../../examples/26_ble_stream/) for
the complete Peripheral startup and advertising flow plus an authenticated
stream consumer. The multi-target
[`bluetooth_stream` hardware gate](03_build_tests.md#jh-ble-stream-v1-hardware-gate)
drives the complete protocol from an independent BlueZ client.
