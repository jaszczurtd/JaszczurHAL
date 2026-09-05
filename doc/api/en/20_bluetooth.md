# Bluetooth Low Energy and Classic APIs

*Also available in [Polish](../pl/20_bluetooth.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

The Bluetooth modules are opt-in. `HAL_ENABLE_BLE` exposes the Low Energy
Peripheral and passive Observer API through `hal/bluetooth/hal_ble.h`.
`HAL_ENABLE_BLUETOOTH_CLASSIC` exposes discovery, pairing, SDP and bonded-peer
management. `HAL_ENABLE_BLUETOOTH_HID_HOST` adds raw Classic HID descriptors
and reports; `HAL_ENABLE_BLUETOOTH_GAMEPAD` adds the normalized gamepad adapter.
`HAL_ENABLE_BLUETOOTH_A2DP_SINK` adds SBC audio reception, while
`HAL_ENABLE_BLUETOOTH_AVRCP_TARGET` adds absolute-volume control. The profile
dependencies are gamepad -> HID Host -> Classic and AVRCP Target -> A2DP Sink
-> Classic. All public APIs are also available from the umbrella
`JaszczurHAL.h` header.

The current release provides one Peripheral connection, connectable legacy
advertising, passive legacy scanning, copied advertising reports, AD structure
parsing, controller and connection events, ATT MTU reporting, and a static
GATT database containing the mandatory GAP and GATT services. It does not yet
provide active scanning or scan-response requests, arbitrary application
characteristics, a GATT client, pairing, or bonding. The opt-in
`HAL_ENABLE_BLE_STREAM` profile adds one fixed authenticated application
service and its notification path. `HAL_ENABLE_BLE_COMMANDS` dedicates that
Stream payload to the shared command router; it does not add a GATT client.

## Supported profiles

| API | Target | Board | Radio/host | Validation |
|---|---|---|---|---|
| BLE | `rp2040` | `picow` | onboard CYW43439 with BTstack | Observer and bare-metal/FreeRTOS Stream hardware gates passed |
| BLE | `rp2350-arm` | `pico2w` | onboard CYW43439 with BTstack | Observer, bare-metal/FreeRTOS Stream, and active Stream+WiFi/MQTT coexistence gates passed |
| BLE | `rp2040` | `pico-rm2` | external PIM730/RM2 CYW43439 over PIO | build-supported; dedicated hardware gate pending |
| BLE | `stm32g474` | `nucleo-g474re-pim730` | external PIM730/RM2 CYW43439 over gSPI | Peripheral and Observer gates plus full bare-metal/FreeRTOS Stream display-load gates passed |
| BLE | `esp32s3` | `waveshare-esp32-s3-zero` | integrated LE controller with ESP-IDF NimBLE | complete compile/link fixture; radio hardware gate pending |
| Classic / HID Host / gamepad | `rp2350-arm` | `pico2w` | onboard CYW43439 with BTstack | hardware-validated with one 8BitDo Zero 2 model 80EH in Android D-input mode; generic Classic XY-BT and a Pico W mouse fixture also passed their respective manager and raw HID gates against this host |
| Classic / HID Host / gamepad | `rp2040` / `stm32g474` | `picow` / `pico-rm2` / `nucleo-g474re-pim730` | CYW43439 with BTstack | build-supported; a dedicated HID/gamepad radio gate has not been passed on these hosts |
| Classic / HID Host / gamepad | `esp32` | `esp32-devkitc-v4` | integrated BR/EDR controller with ESP-IDF Bluedroid and ESP HID Host | complete compile/link fixture; generic radio hardware gate pending |
| A2DP Sink / AVRCP Target | `rp2040` | `picow` | onboard CYW43439 with BTstack and Bluedroid SBC decoder | hardware-validated with an Android source, 44.1 kHz stereo SBC, filtered/amplified GP6 PWM output, pause/resume/stop, absolute volume, and bonded reconnect after watchdog and cold boot |
| A2DP Sink / AVRCP Target | `rp2350-arm` | `pico2w` | onboard CYW43439 with BTstack and Bluedroid SBC decoder | hardware-validated with a BlueZ source at 48 kHz stereo for more than 30 minutes, including pause/resume/stop, absolute volume, and reconnect; a product must still validate its selected physical audio output |
| BLE and Classic profiles | `mock` | `host-mock` | deterministic test backends | Classic, non-gamepad HID and gamepad host tests |

The RP2350 backend supports Pico 2 W only with the `rp2350-arm` target. Pico 2
W with `rp2350-riscv` is unsupported because the CYW43 Bluetooth transport is
not enabled for that target. `HAL_ENABLE_BLE` and
`HAL_ENABLE_BLUETOOTH_CLASSIC` are rejected at compile time when their exact
transport is unavailable. Runtime checks distinguish
`HAL_BOARD_CAP_BLUETOOTH_LE_CONTROLLER` from
`HAL_BOARD_CAP_BLUETOOTH_CLASSIC_CONTROLLER`; the older generic Bluetooth bit
remains available for compatibility. External modules additionally require
`HAL_BOARD_CAP_EXTERNAL_RADIO_FRONTEND`.

ESP32-S3 currently supports the base BLE Peripheral/Observer API, but not
`HAL_ENABLE_BLE_STREAM`, a GATT client, or Classic. The original ESP32 supports
the Classic manager, HID Host and gamepad adapter, but does not enable the
public BLE API. These are deliberate target boundaries, not runtime fallbacks.

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

`hal_ble_initialize()` starts the selected target backend and returns when
startup has been accepted. Readiness is asynchronous and is reported by
`HAL_BLE_EVENT_CONTROLLER_READY`. Initialization and deinitialization are
idempotent after success. Deinitialization invalidates every connection and
advertising handle, clears the event queue, and removes the callback.

Call `hal_ble_poll()` frequently from one task or cooperative loop. It services
the controller and then dispatches callbacks outside the backend's radio
lock. Calling `hal_ble_poll()`, changing the callback, or deinitializing
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
`HAL_BLE_EVENT_DISCONNECTED`. `hal_ble_get_mtu()` returns 23 until the selected
stack reports a negotiated value through `HAL_BLE_EVENT_MTU_UPDATED`.

## ESP-IDF backend behavior

ESP32-S3 uses NimBLE for the base LE API. ESP-IDF callbacks copy addresses,
advertising payloads, connection state, and MTU changes into bounded HAL
queues. Application callbacks still run only from `hal_ble_poll()` and never
from an ESP-IDF event task.

The original ESP32 uses Bluedroid for generic Classic inquiry, SDP, pairing and
native bond removal, plus `esp_hidh` when HID Host is selected. The shared
backend does not filter names, Class of Device, VID/PID, report descriptors or
report contents. The gamepad adapter alone applies its device policy and
descriptor parser. PIN `0000` and SSP confirmation remain pending until the
application explicitly authorizes or rejects the request.

Both ESP backends share one idempotent NVS initializer with the network
backend. An incompatible or full NVS partition returns `HAL_ECONFIG`; the HAL
does not erase application storage automatically. The HAL parser, event
queues, and snapshots have fixed capacity. ESP-IDF's NimBLE, Bluedroid, event
loop, and HID Host components may allocate their own internal objects.

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

Use `hal_ble_get_info()` for a consistent state snapshot, local and peer
addresses, current handles, generation, last status, MTU, scan state, pending
report count, and both drop counters. A fatal controller or transport error moves the subsystem to
`HAL_BLE_STATE_FAILED`, invalidates its handles, stops scanning, and advances
the generation.

## Bluetooth Classic manager and profiles

### Classic manager

`HAL_ENABLE_BLUETOOTH_CLASSIC` adds `hal_bluetooth_classic.h`. Open one opaque
manager, call `hal_bluetooth_classic_poll()` frequently, and close every
attached profile before closing the manager. Inquiry results are copied into a
bounded queue and contain BD_ADDR, bounded name, Class of Device, optional
RSSI, and an SDP service bit mask. `hal_bluetooth_classic_sdp_query()` updates
a discovered peer through the same queue. `HAL_EOVERFLOW` acknowledges dropped
results; call again to drain retained entries.

Pairing is explicit. `hal_bluetooth_classic_pair()` starts bonding where the
backend supports a profile-independent request. A pending Just Works, PIN or
passkey request is visible in `hal_bluetooth_classic_info_t`; the application
must call `hal_bluetooth_classic_pairing_authorize()` or
`hal_bluetooth_classic_pairing_reject()`. Authorization must follow a trusted
local gesture. A name, address or unauthenticated exchange is not user identity.

An incoming profile can instead use
`hal_bluetooth_classic_pairing_window_open()`. During this bounded window the
device is connectable and discoverable, and an application may authorize the
reported pairing request. After expiry it remains connectable for restored
peers, becomes non-discoverable, and rejects unknown pairing attempts. Set the
shared local name and 24-bit Class of Device with
`hal_bluetooth_classic_set_identity()` before opening the window.

`hal_bluetooth_classic_open_ex()` accepts an indexed
`hal_bluetooth_classic_bond_provider_t`. Each opaque record contains one peer,
format version, profile verification-rules identifier, sequence, link-key type,
one link key and CRC. The manager is the only owner of link-key persistence.
Providers store records unchanged and can use any medium. The optional
`jh_bluetooth_classic_bond_kv_provider()` maps consecutive slots to consecutive
`hal_kv` keys. Invalid records are ignored during restore. A profile calls
`hal_bluetooth_classic_peer_save()` only after it has validated the peer and
flowing data; the manager commits it later from `poll()`, outside stack
callbacks. Peers can be enumerated or forgotten by address.

On BTstack/CYW43, link keys are copied from the HCI notification and restored
to the bounded BTstack database. Bluedroid keeps actual link keys in its own
NVS and does not expose them; therefore portable provider storage returns
`HAL_EUNSUPPORTED` on original ESP32, while RAM enumeration/native reconnect
and `hal_bluetooth_classic_peer_forget()` remain available. A generic
`hal_bluetooth_classic_pair()` is also unsupported there; connecting the
selected profile starts authentication.

`hal_bluetooth_classic_peer_forget_all()` is the shared factory-reset entry
point for multi-profile products. For each peer it erases the provider slot
before removing the corresponding native bond; a storage failure leaves that
peer's runtime state intact so the reset can be retried.

### A2DP Sink and AVRCP Target

`HAL_ENABLE_BLUETOOTH_A2DP_SINK` adds `hal_bluetooth_a2dp_sink.h` and implies
the Classic manager. One Sink attaches to one open manager and accepts SBC at
44.1 or 48 kHz in mono, stereo, or joint-stereo mode. The public API contains
no BTstack or audio-driver types. It yields signed 16-bit interleaved PCM in
the negotiated channel count, or saturating mono when configured with
`HAL_BLUETOOTH_A2DP_OUTPUT_MONO`.

Stack callbacks only copy complete media packets into a bounded queue. Call
`hal_bluetooth_classic_poll()` to service the shared controller, then drain
`hal_bluetooth_a2dp_sink_poll()` until it returns `HAL_EAGAIN`. Parsing, SBC
decoding, software volume, downmixing and small clock corrections all run in
that poll context. `hal_bluetooth_a2dp_sink_pcm_next()` applies a fixed
prebuffer after stream start and after an underrun. The application owns the
physical output and should move returned PCM into its own ready buffers; a DMA
interrupt should only select a ready buffer or silence.

`hal_bluetooth_a2dp_sink_info_t` exposes stream format and state, packet loss,
bounded packet/PCM queue levels and high-water marks, drops, corrupt frames,
PCM overflow/underrun counts and current clock correction. Queue pressure
never causes an unbounded allocation. A new peer is committed to the shared
Classic bond provider only after local pairing authorization, a captured link
key and the first successfully decoded SBC frame. Its profile identifier is
`HAL_BLUETOOTH_A2DP_SINK_PROFILE_ID`.

`HAL_ENABLE_BLUETOOTH_AVRCP_TARGET` adds
`hal_bluetooth_avrcp_target.h` and implies A2DP Sink. The minimal Target
accepts Controller absolute-volume changes from 0 through 127, coalesces a
pending change to the newest value, and can report the current local value to
a subscribed Controller. It shares the A2DP/Classic connection and bond; it
never creates another stored key. Close AVRCP first, then A2DP, then Classic.
The complete C consumer and PWM/DMA output adapter are in
[`examples/30_bluetooth_speaker`](../../../examples/30_bluetooth_speaker/).

### Generic HID Host

`HAL_ENABLE_BLUETOOTH_HID_HOST` adds `hal_bluetooth_hid_host.h` and implies the
Classic manager. One HID Host handle attaches to an open manager and supports
one active HID connection. It exposes a copied report descriptor plus a
bounded queue of raw Input, Output and Feature reports without interpreting a
device class. Applications can send Output/Feature reports and request
Input/Feature reports. Device selection and descriptor validation belong to
the consuming application or profile adapter. Closing HID disconnects an
active link but leaves its Classic manager open.

The deterministic mock can inject Classic readiness, inquiry/SDP results,
pairing, link keys, generic descriptors and raw reports. Host tests include a
mouse descriptor to prove that the shared HID path contains no gamepad filter.
The `classic-scan` and `hid-host` variants of
[`examples/29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/)
compile these layers without `HAL_ENABLE_BLUETOOTH_GAMEPAD`.

### Gamepad adapter

The gamepad API is an adapter over the Classic manager and generic HID Host. It
owns those handles internally and returns one opaque
`hal_gamepad_t`. `hal_gamepad_open()` starts the profile asynchronously;
`hal_gamepad_poll()` must then run frequently from one task or cooperative
loop. `hal_gamepad_get_info()` reports the public state, last status, current
connection generation, pairing flags, whether a known device is stored, and
bounded-queue diagnostics.

The supported public states are `UNINITIALIZED`, `STARTING`, `READY`,
`DISCOVERING`, `CONNECTING`, `CONNECTED`, and `FAILED`. A fatal controller or
transport error moves the profile to `FAILED`. `hal_gamepad_close()` stops the
profile, clears the selected device, and invalidates its handle.

#### Pairing and reconnect

Pairing is application-controlled and bounded. Once the profile reaches
`READY`, call `hal_gamepad_pairing_open()` to start its discovery window. This
is also allowed when a known device exists and lets the application replace it.
When
`hal_gamepad_info_t::pairing_pending` becomes true, the application may call
`hal_gamepad_pairing_authorize()` to accept Just Works or the legacy PIN
`0000`. Unsupported passkey flows are rejected. The accepted address identifies
the known device used by `hal_gamepad_reconnect()` during the current open
profile session. Without a bond provider (see below), link-key storage is
stack-specific RAM: closing the profile or restarting firmware clears the
HAL-selected identity, so the application must be prepared to open a new
pairing window afterward.

Opening a pairing window is a deliberate authorization action. Products
should expose it only after local user input and should not treat a device
name, Bluetooth address, or an unauthenticated Just Works exchange as proof of
user identity.

The hardware-validated gamepad combination is one 8BitDo Zero 2 model 80EH in
Android D-input mode on `rp2350-arm:pico2w`. Start that mode with `B+Start`,
then hold `Select` until the pairing LED flashes while the application pairing
window is open. Later reconnects use the normal `Start` power-on path and do
not reopen pairing. The controller's Switch and macOS modes advertise separate
Bluetooth identities and are not part of this support declaration. Other
gamepads, Zero 2 modes, and HID-capable boards require their own descriptor,
report, reconnect, and resource gate.

#### Persistent bonding

`hal_gamepad_open_ex(&handle, bond_provider)` accepts an optional
`hal_gamepad_bond_provider_t` -- `load()`/`store()`/`erase()` hooks over an
opaque, fixed-size `hal_gamepad_bond_blob_t`. `hal_gamepad_open()` is
equivalent to `hal_gamepad_open_ex(&handle, NULL)` and keeps the prior
RAM-only behavior. This legacy one-slot provider is bridged into the indexed
Classic manager; the gamepad decides when its profile checks pass, while the
manager owns encoding, link keys and persistence timing:

```c
#include <hal/bluetooth/hal_gamepad.h>
#include <hal/bluetooth/jh_gamepad_bond_kv_provider.h>

hal_gamepad_t gamepad;
jh_gamepad_bond_kv_context_t bond_context;
const hal_gamepad_bond_provider_t provider =
    jh_gamepad_bond_kv_provider(&bond_context, MY_BOND_KV_KEY);
hal_gamepad_open_ex(&gamepad, &provider);
```

`jh_gamepad_bond_kv_provider()` (declared in
`hal/bluetooth/jh_gamepad_bond_kv_provider.h`, active when both
`HAL_ENABLE_BLUETOOTH_GAMEPAD` and `HAL_ENABLE_KV` are enabled) is a ready
adapter over `hal_kv_set_blob_ex()`/`get_blob_ex()`/`delete_ex()`; a consumer
that wants a different persistent medium implements the three-function
provider directly instead. `hal_kv_init_ex()` must already have succeeded
before the provider is used and must stay initialized for as long as the
gamepad profile is open. The caller-owned `bond_context` must remain valid for
the same lifetime.

A new gamepad peer reaches the common Classic bond record only after full
acceptance -- local pairing
authorization, matched identity, an accepted report descriptor, at least one
HID report (proof the link is genuinely flowing data), and a captured link
key. Until then the previous bond, if any, stays active. `store()`/`erase()`
are called only from `hal_gamepad_poll()`, after the backend has returned from
any Bluetooth stack callback and released the radio lock -- never from inside
a stack callback or while a lock is held. At `hal_gamepad_open_ex()`, a stored
blob is validated (magic, format version, CRC, and the peer-verification
rules baked into the running firmware) before its link key is reinstalled
into the controller; a structurally invalid or stale-rules blob is treated
exactly like "no bond" rather than trusted.

`hal_gamepad_forget()` is the factory-reset entry point: it disconnects any
active link, clears the known peer from the controller and from RAM, and
erases the persisted blob through the bond provider (a no-op when none was
given). A subsequent `hal_gamepad_pairing_open()` starts a fresh pairing.
Persistent storage is erased before the manager drops its runtime peer. If
the provider reports an error, the peer remains known so the application can
retry the factory reset and the old bond cannot silently return after reboot.

Backend-specific link-key behavior is described under the Classic manager
above. `hal_gamepad_forget()` delegates native bond removal and provider erase
to that manager.

#### Normalized snapshots

The HID report parser does not depend on BTstack or ESP-IDF. It validates a
bounded report descriptor and feeds the same normalized snapshot model on
every backend. Well-formed HID long items with unsupported tags are skipped;
truncated long items reject the descriptor.

`hal_gamepad_snapshot_t` contains a connection generation, a 32-bit button
mask, nine generic-desktop axes, an axis-presence mask, a D-pad direction mask,
and the connection state. Button bit 0 represents HID Button 1. Axes are
normalized to `-32767..32767` and indexed by `HAL_GAMEPAD_AXIS_*`; unsupported
axes have their bit clear in `axes_present`. The D-pad combines
`HAL_GAMEPAD_DPAD_UP`, `RIGHT`, `DOWN`, and `LEFT`.

`hal_gamepad_snapshot()` reads the latest state without consuming it.
`hal_gamepad_snapshot_next()` pops changes from the fixed
`HAL_GAMEPAD_SNAPSHOT_QUEUE_DEPTH` queue. It returns `HAL_EAGAIN` when empty.
If intermediate changes were dropped, it first returns `HAL_EOVERFLOW`; call
again to receive the newest retained sequence. Connection and disconnection
also produce snapshots, and the disconnection snapshot clears every input so
an application cannot retain a pressed control after a lost link.
`hal_gamepad_disconnect()` only accepts the request; completion is asynchronous.
Applications must observe state or snapshots and must not depend on whether a
backend completes before or during the next `hal_gamepad_poll()`.

```c
hal_gamepad_t gamepad = NULL;

void service_gamepad(void) {
  hal_status_t status = hal_gamepad_poll(gamepad);
  if (status != HAL_OK && status != HAL_EOVERFLOW) {
    return;
  }

  for (;;) {
    hal_gamepad_snapshot_t snapshot = {0};
    status = hal_gamepad_snapshot_next(gamepad, &snapshot);
    if (status == HAL_EOVERFLOW) {
      continue;
    }
    if (status != HAL_OK) {
      break;
    }
    /* Consume buttons, axes and D-pad state. */
  }
}
```

The deterministic mock supports both normalized compatibility injection and
the complete raw Classic -> HID -> parser path. The complete C consumer,
isolated Classic/HID variants, and BLE+Classic build variant are in
[`examples/29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/).

## JH BLE Stream v1

`HAL_ENABLE_BLE_STREAM` adds `hal_ble_stream.h`, a bounded byte stream carried
by one static GATT service. The flag enables `HAL_ENABLE_BLE` and
`HAL_ENABLE_CRYPTO`.

BLE Stream remains a general application byte stream when selected alone. The
separate [`hal_ble_commands`](23_commands.md#authenticated-ble-stream-adapter)
module fragments the shared binary command format across authenticated Stream
payloads and dispatches requests through `hal_command_router`.
`HAL_ENABLE_BLE_STREAM` does not enable that behavior or the router;
`HAL_ENABLE_BLE_COMMANDS` enables both dependencies and gives the command
adapter exclusive ownership of Stream payload send/receive operations.

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
owns the radio lock. Stream keeps at most one backend-accepted notification in
flight, and `pending_tx` includes that notification in addition to locally
queued payloads.

Before accepting a new `HELLO`, Stream discards a notification
that is still staged in the backend. If local submission or its completion
callback is already in progress, the `HELLO` is refused with `HAL_EBUSY` and
the current session remains available for a retry. Any other discard failure
closes the session without sending `HELLO_ACK`. This prevents an in-link rekey
from overtaking data from the previous session.

Stream initialization and deinitialization serialize service publish and
unpublish operations. A concurrent lifecycle call returns `HAL_EBUSY`; failed
publication rolls the Stream state back to `HAL_BLE_STREAM_STATE_UNINITIALIZED`.

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

`hal_ble_stream_receive_ex()` has the same queue and overflow behavior while
also returning immutable provenance for the popped DATA payload: Stream
generation, public handshake session identifier and authenticated directional
counter. Stream adapters use it to prevent fragments from different sessions
or counter ranges from being combined. The original
`hal_ble_stream_receive()` remains the convenience form when that metadata is
not needed.

`hal_ble_stream_get_info()` reports the state, negotiated capabilities,
public session identifier, directional counters, authentication failures,
replay rejections and queue depth for diagnostics.

## Bluetooth coexistence and ownership

BLE, Bluetooth Classic, and WiFi share one CYW43 controller, transport, radio
runtime, and service lock. Applications must not link Pico SDK
`pico_cyw43_arch` or `pico_btstack_cyw43` alongside this backend. BLE callbacks
are deferred until after radio servicing, so application code never runs under
that lock. BLE-only, Classic-only, and combined BLE+Classic images use the same
reference-counted Bluetooth host. Active gamepad+BLE coexistence on hardware is
not yet a release gate.

The 2026-08-25 Pico 2 W active coexistence gate kept an authenticated Stream
connection active while MQTT traffic forced a WiFi disconnect and reconnect.
Both bare-metal and FreeRTOS sustained 10.00 BLE echoes/s for more than 607 s
with zero loss. Bare-metal completed 6079/6079 echoes (94.7 ms mean, 249.0 ms
maximum latency); FreeRTOS completed 6077/6077 (93.7 ms mean, 204.1 ms
maximum).

Each run carried 34 BLE echoes through the WiFi reconnect window,
re-established WiFi and MQTT, retained both radio-runtime references, and
reported no BLE, Stream, MQTT, HCI, queue, or event errors. The measured
maximum BLE poll time was 4.768 ms bare-metal and 5.618 ms FreeRTOS. The final
FreeRTOS rerun used the strengthened MQTT-progress oracle: its observation
delta was 5794 echoes at 9.66 Hz, with zero stagnant one-second summaries.

## License and distribution boundary

Bluetooth firmware links the project-maintained fork of BlueKitchen BTstack
from the exact revision recorded in `third_party/btstack_version.conf`.
JaszczurHAL applies no local source patches. Three relevant license texts are
tracked:

- the standard BlueKitchen
  [`third_party/LICENSE.BTstack`](../../../third_party/LICENSE.BTstack) grant
  permits redistribution, use, and modification only for personal benefit and
  not for commercial purpose or monetary gain. Its source and binary
  redistribution conditions require the copyright notice, conditions, and
  disclaimer to be retained or reproduced as specified in that text;
- the separate Raspberry Pi
  [`src/hal/bluetooth/LICENSE.RP`](../../../src/hal/bluetooth/LICENSE.RP) grant
  applies to a `Customer`, defined as a purchaser of a listed `Product`. It
  permits that Customer to use, modify, integrate, and distribute BTstack only
  with the defined `Products` or `Customer Products`. The listed Products are
  Pico W, Pico WH, Pico 2 W, Pico 2 WH, and RM2; Customer Products are products
  manufactured or distributed by Customers which use or are derived from
  those Products. This is a product-scoped grant, not a general permission for
  every board or device containing a CYW43 controller;
- the Bluedroid SBC decoder bundled with BTstack retains Apache-2.0 copyright
  notices from Android Open Source Project, Broadcom, and Open Interface. The
  complete tracked text is
  [`third_party/LICENSE.BLUEDROID-SBC`](../../../third_party/LICENSE.BLUEDROID-SBC),
  and the codec is inventoried separately in the generated SBOM.

The applicable grant depends on the physical product and its distribution.
Review the complete tracked license texts and satisfy the conditions of the
grant relied upon; uses outside them may require a separate BlueKitchen
license. This section is a technical inventory and is not legal advice. These
conditions apply to BTstack-enabled artifacts, not to JaszczurHAL builds that
do not compile BTstack.

See the buildable [`26_ble_stream` example](../../../examples/26_ble_stream/) for
the complete Peripheral startup and advertising flow plus an authenticated
stream consumer. The multi-target
[`bluetooth_stream` hardware gate](03_build_tests.md#jh-ble-stream-v1-hardware-gate)
drives the complete protocol from an independent BlueZ client.

See [`29_bluetooth_gamepad`](../../../examples/29_bluetooth_gamepad/) for the
Classic HID snapshot flow and its combined BLE+Classic build variant.

See [`30_bluetooth_speaker`](../../../examples/30_bluetooth_speaker/) for the
A2DP Sink lifecycle, bounded pairing policy, shared bond, optional AVRCP
absolute volume, diagnostics, and DMA PWM output adapter.
