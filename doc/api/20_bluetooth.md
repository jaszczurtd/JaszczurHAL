# Bluetooth Low Energy Peripheral and Observer API

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

The `hal_ble` module is an **experimental**, opt-in Bluetooth Low Energy
Peripheral and passive Observer API. Define `HAL_ENABLE_BLE` and include
`hal/hal_ble.h` or the umbrella `JaszczurHAL.h` header.

The current release provides one Peripheral connection, connectable legacy
advertising, passive legacy scanning, copied advertising reports, AD structure
parsing, controller and connection events, ATT MTU reporting, and a static
GATT database containing the mandatory GAP and GATT services. It does not yet
provide active scanning or scan-response requests, application
characteristics, notifications, a GATT client, pairing, bonding, or a BLE
stream.

## Supported profiles

| Target | Board | Radio |
|---|---|---|
| `rp2040` | `picow` | onboard CYW43439 |
| `stm32g474` | `nucleo-g474re-pim730` | external PIM730/RM2 CYW43439 over gSPI |
| `mock` | `host-mock` | deterministic test backend |

The Pico 2 W descriptor records the physical Bluetooth controller, but the
public backend is not yet enabled for RP2350. `HAL_ENABLE_BLE` is rejected at
compile time on unsupported targets and on profiles without a Bluetooth
controller. Runtime capability checks use
`HAL_BOARD_CAP_BLUETOOTH_CONTROLLER` and additionally require
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND` for an external module.

## Lifecycle and polling

```cpp
hal_status_t status = hal_ble_initialize();
if (status == HAL_OK) {
  status = hal_ble_set_event_callback(on_ble_event, context);
}

for (;;) {
  status = hal_ble_poll();
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

## WiFi coexistence and ownership

BLE and WiFi share one CYW43 controller, transport, radio runtime, and service
lock. Applications must not link Pico SDK `pico_cyw43_arch` or
`pico_btstack_cyw43` alongside this backend. BLE callbacks are deferred until
after radio servicing, so application code never runs under that lock.

## License and distribution boundary

BLE firmware links BlueKitchen BTstack from the exact revision recorded in
`third_party/btstack_version.conf`. Its license permits redistribution and use
only for personal benefit and not for commercial purpose or monetary gain.
Distributors must reproduce the tracked
[`third_party/LICENSE.BTstack`](../../third_party/LICENSE.BTstack) notice in
source or binary distribution materials. Commercial products require a
separate license from BlueKitchen. This restriction applies to BLE-enabled
artifacts, not to JaszczurHAL builds that do not compile BTstack.

See the buildable [`58_ble_peripheral` example](../../examples/58_ble_peripheral/)
for the complete startup and advertising flow.
