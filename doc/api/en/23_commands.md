# Transport-neutral command routing

*Also available in [Polish](../pl/23_commands.md).*

> **Part of [JaszczurHAL API Reference](../../en/JaszczurHAL_API.md)**

The command subsystem separates command registration and dispatch from the
transport that carries a request. `hal_command_router` owns named handlers and
their source/security policy. `hal_command_wire` provides a bounded binary
message format for packet and stream adapters. The implemented adapters are
the HTTP/WebSocket compatibility layer in `hal_net_commands`, framed serial
sessions in `hal_serial_commands`, the reliable LoRa adapter in
`hal_lora_commands`, and the authenticated BLE Stream adapter in
`hal_ble_commands`.

## Enable the modules

Enable only the router for direct dispatch or a custom adapter:

```c
#define HAL_ENABLE_COMMAND_ROUTER
```

Enable framed serial command dispatch with:

```c
#define HAL_ENABLE_SERIAL_COMMANDS
```

This propagates `HAL_ENABLE_COMMAND_ROUTER`. Serial Session framing itself
remains available without the adapter.

Enable the LoRa adapter together with one radio provider:

```c
#define HAL_ENABLE_SX126X
#define HAL_ENABLE_LORA_COMMANDS
```

`HAL_ENABLE_LORA_COMMANDS` propagates `HAL_ENABLE_COMMAND_ROUTER` and
`HAL_ENABLE_LORA_LINK`; the link then propagates `HAL_ENABLE_LORA` and
`HAL_ENABLE_CRC`. Enable the BLE adapter with:

```c
#define HAL_ENABLE_BLE_COMMANDS
```

`HAL_ENABLE_BLE_COMMANDS` propagates `HAL_ENABLE_BLE_STREAM` and
`HAL_ENABLE_COMMAND_ROUTER`; BLE Stream then propagates BLE and CRYPTO.
`HAL_ENABLE_NET_COMMANDS` also propagates the router while retaining its HTTP,
WebSocket, cJSON, TCP and WiFi dependencies.

## Router

```c
#include <hal/commands/hal_command_router.h>
```

A request contains binary-safe arguments, an encoding, a non-owning command
name and source context, plus request, peer and session identifiers. Source and
security masks let one handler accept only selected entry points. The router
checks those masks before invoking the handler synchronously.

The process-wide default router is shared by transport adapters. Independent
routers are available when an application needs an isolated handler set.

```c
static hal_status_t echo_command(const hal_command_request_t *request,
                                 hal_command_response_t *response,
                                 void *user) {
  (void)user;
  return hal_command_response_write(response, request->arguments,
                                    request->arguments_length);
}

hal_command_router_t router = NULL;
hal_status_t status = hal_command_router_default(&router);
if (status != HAL_OK) {
  return status;
}

hal_command_definition_t echo = {
    .name = "echo",
    .allowed_sources = HAL_COMMAND_SOURCE_MASK_ALL,
    .required_security = 0u,
    .handler = echo_command,
    .user = NULL,
};
status = hal_command_router_register(router, &echo);
```

Registration copies the definition and command name into a bounded slot. A
second registration with the same name replaces an idle slot. Unregistering,
replacing, clearing or destroying a router while an affected handler is active
returns `HAL_EBUSY`; destroying the default router returns `HAL_EPERM`.
Exhausting the router pool or handler slots returns `HAL_ENOMEM`, and a stale
handle returns `HAL_EUNINIT`. Request pointers and `source_context` are borrowed
only for the duration of the callback.

A service that shares a router and must not replace another owner's command can
use `hal_command_router_register_unique()`. The operation returns `HAL_EEXIST`
without changing the registered slot when the name is already present. On
shutdown, `hal_command_router_unregister_if_matches()` removes the command only
when both its public C handler and `user` pointer match. A different owner or an
active matching handler returns `HAL_EBUSY`; an absent name returns
`HAL_ENOENT`. Registration and ownership checks are performed under the router
lock, so startup, rollback and shutdown do not contain a check-then-change
window.

Dispatch does not serialize handler execution. The same handler can run
concurrently when multiple tasks or transport adapters dispatch it, so shared
`user` state must provide its own synchronization.

`hal_command_response_write()` and `hal_command_response_write_str()` append to
the fixed response buffer. The encoding helper also selects the usual content
type. Overflow is reported as `HAL_EOVERFLOW` and recorded in
`response.overflow`. The `message` and `content_type` pointers are borrowed;
values supplied by a handler must remain valid for as long as the caller uses
the completed response.

Dispatch resets the response before lookup. An explicit non-`HAL_OK`
`response.status` takes precedence over a successful handler return. If a
handler returns an error while the response is still successful, that return
becomes the response status. `HAL_NONE` from either path is normalized to
`HAL_EINTERNAL`. Source, encoding and message type string helpers return
`"UNKNOWN"` for values outside their enums.

```c
hal_command_request_t request = {
    .source = HAL_COMMAND_SOURCE_DIRECT,
    .encoding = HAL_COMMAND_ENCODING_TEXT,
    .command = "echo",
    .arguments = (const uint8_t *)"hello",
    .arguments_length = 5u,
    .request_id = 1u,
};
hal_command_response_t response;
status = hal_command_router_dispatch(router, &request, &response);
```

The defined sources are direct calls, HTTP, WebSocket, Serial Session, reliable
LoRa and BLE Stream. Security flags describe authentication, encryption,
integrity and replay protection reported by the adapter. The router enforces
requested bits but does not perform transport security itself.

## Wire messages

```c
#include <hal/commands/hal_command_wire.h>
#include <string.h>
```

The wire helper encodes one `REQUEST`, `RESPONSE` or `EVENT` into caller-owned
storage and decodes exactly one complete message into bounded owned fields.
`hal_command_message_frame_size()` lets packet and stream adapters discover the
first complete frame incrementally. For example:

```c
hal_command_message_t message = {0};
message.type = HAL_COMMAND_MESSAGE_REQUEST;
message.encoding = HAL_COMMAND_ENCODING_TEXT;
message.request_id = 17u;
memcpy(message.name, "echo", sizeof("echo"));
memcpy(message.payload, "hello", 5u);
message.payload_length = 5u;

uint8_t frame[128];
size_t frame_length = 0u;
hal_status_t status = hal_command_message_encode(
    &message, frame, sizeof(frame), &frame_length);

hal_command_message_t decoded;
if (status == HAL_OK) {
  status = hal_command_message_decode(frame, frame_length, &decoded);
}
```

Version 1 starts with a 16-byte big-endian header:

| Offset | Field |
|---:|---|
| 0..1 | ASCII `JC` marker |
| 2 | wire version |
| 3 | message type |
| 4 | argument/payload encoding |
| 5 | command or event name length |
| 6..7 | reserved, zero |
| 8..11 | request identifier |
| 12..13 | signed `hal_status_t` response value |
| 14..15 | payload length |
| 16.. | name followed by payload |

A request has a nonzero identifier, a name and `HAL_NONE` status. An event has
a name, identifier zero and `HAL_NONE` status. A response has a nonzero
identifier, no name and a status other than `HAL_NONE`. The decoder rejects
unknown versions, invalid enums, nonzero reserved bytes, malformed names and
any size mismatch with `HAL_EPROTO`.

If encoder output storage is too small, it returns `HAL_EOVERFLOW` and writes
the required size to `out_length` without producing a partial message.
`hal_command_source_to_string()`, `hal_command_encoding_to_string()` and
`hal_command_message_type_to_string()` provide stable diagnostic names.

For a partial stream buffer, `hal_command_message_frame_size()` returns
`HAL_EAGAIN`. Once the fixed header is present, it also reports the required
total length; once that many bytes are buffered, it returns `HAL_OK` even when
another frame follows. Pass exactly the reported prefix to the decoder and
retain trailing bytes for the next call. `HAL_COMMAND_WIRE_MAX_FRAME_SIZE`
provides the compile-time upper bound for adapter-owned storage. The wire
format does not add encryption or authentication; those properties belong to
the transport adapter.

## Framed Serial Session adapter

```c
#include <hal/serial/hal_serial_commands.h>
```

Initialize `hal_serial_session`, register router handlers using their existing
SC names, then attach one caller-owned adapter:

```c
static hal_serial_session_t session;
static hal_serial_commands_t serial_commands;

hal_serial_session_init_with_vocabulary(
    &session, "ECU", "1.2.3", "build-id", &serial_vocabulary);

hal_serial_commands_config_t config =
    hal_serial_commands_config_defaults(&session);
config.router = router;
config.command_prefix = "SC_";

hal_status_t status = hal_serial_commands_init(&serial_commands, &config);

/* In the application loop: */
hal_serial_session_poll(&session);
```

The buildable
[`28_serial_commands`](../../../examples/28_serial_commands/README.md) project
shows the complete lifecycle with an independent router, `echo` and `info`
handlers, source policy, framed requests and startup rollback.

`hal_serial_commands_init()` claims the session's unknown-payload callback and
returns `HAL_EBUSY` instead of replacing an existing callback. HELLO, BYE and
the optional authentication exchange stay in `hal_serial_session`. A matching
application payload is accepted only after HELLO activates the session. The
adapter splits the first whitespace-delimited token from the remaining
arguments without renaming it, so `SC_GET_PARAM nominal_rpm` dispatches the
router command `SC_GET_PARAM` with `nominal_rpm` as TEXT or JSON arguments.

The optional `allow_inactive(request, user)` predicate can admit selected
matching commands before HELLO. Its true result only reaches normal router
policy; it does not add authentication or bypass source/security checks. This
supports operations such as a router-owned bootloader reboot that must retain
its existing unauthenticated `NOT_AUTHORIZED` response before session setup.
Leave the callback NULL to keep every matching command HELLO-gated.

The request uses `HAL_COMMAND_SOURCE_SERIAL_SESSION`, the SC frame sequence as
`request_id`, the Serial Session identifier as `session_id`, the configured
`peer_id`, and the session pointer as `source_context`. An authenticated
session contributes only `HAL_COMMAND_SECURITY_AUTHENTICATED`. Serial Session
does not report encryption, cryptographic message integrity or replay
protection to router policy.

A non-empty TEXT or JSON handler body is emitted verbatim in a response frame
with the same sequence. Empty and non-text responses use the optional
`formatter` callback. This lets a project map router statuses to its existing
SC reply vocabulary without embedding project-specific tokens in JaszczurHAL.
Without a formatter, the adapter emits `OK` or `ERR <HAL_STATUS>`.

`command_prefix` is optional and remains part of the routed command name. When
it is set, a non-matching payload is passed to the optional `fallback`
callback; this supports existing diagnostic commands outside the SC namespace.
Without a fallback, the adapter uses the session vocabulary's unknown-command
reply. A matching but unregistered SC command always reaches the router and
therefore remains visible to the response formatter.

Responses are bounded by `HAL_SERIAL_FRAME_PAYLOAD_MAX` and reject embedded
NUL, `*`, CR and LF. The adapter accepts only TEXT and JSON encoding because SC
payloads are line-oriented. `hal_serial_commands_get_last_status()` retains
the last router, formatting or payload error. `hal_serial_commands_deinit()`
returns `HAL_EBUSY` during the inactive predicate, handler, formatter, fallback
or response emit, and clears the session callback only when the adapter still
owns it.

## Reliable LoRa adapter

```c
#include <hal/radio/hal_lora_commands.h>
```

Create and initialize the raw radio and reliable link first. The link must be
in its receiving state when the adapter is attached. A null router in the
configuration selects the shared default router.

```c
hal_lora_commands_config_t config =
    hal_lora_commands_config_defaults(link, 7u);
config.router = router;
config.acknowledged = true;
config.initial_request_id = 1u;

hal_lora_commands_t commands = NULL;
hal_status_t status = hal_lora_commands_create(&config, &commands);
if (status != HAL_OK) {
  return status;
}

uint32_t request_id = 0u;
status = hal_lora_commands_request_start(
    commands, UINT16_C(0x1002), "echo", HAL_COMMAND_ENCODING_TEXT,
    "hello", 5u, &request_id);
```

The adapter copies and encodes a request before starting the link send. A
request identifier is nonzero and advances only after the link accepts the
send; `HAL_EBUSY` or `HAL_EAGAIN` leaves the counter unchanged and writes zero
to `out_request_id`. The `acknowledged` setting applies to point-to-point
requests, responses and events.

`hal_lora_commands_event_start()` sends a named event with identifier zero.
Broadcast events always disable the transport acknowledgement regardless of
the configured setting. Requests cannot use the broadcast address.

After attachment the adapter exclusively advances and receives from the link.
Call `hal_lora_commands_process()` instead of
`hal_lora_link_process()`/`hal_lora_link_receive()`. Incoming requests are
decoded, dispatched synchronously and answered automatically. Received
responses and events are copied out with `hal_lora_commands_receive()`:

```c
hal_status_t process_status = hal_lora_commands_process(commands);
if (process_status == HAL_OK || process_status == HAL_EAGAIN ||
    process_status == HAL_IGNORED) {
  hal_command_message_t incoming;
  hal_lora_link_message_info_t link_info;
  if (hal_lora_commands_receive(commands, &incoming, &link_info) == HAL_OK) {
    /* Match RESPONSE messages with request_id; consume EVENT messages by name. */
  }
}
```

`hal_lora_commands_receive()` returns `HAL_EAGAIN` without consuming anything
when no response or event is queued. After successful destruction, a valid API
call using the old handle returns `HAL_EUNINIT`.

`hal_lora_commands_process()` has one logical owner. A concurrent or reentrant
call returns `HAL_EBUSY`. The adapter releases its own mutex while invoking a
handler, so that handler may safely query adapter state, consume an already
queued application message, or attempt a request or event; a send attempted
while the link is acknowledging the incoming request normally returns
`HAL_EBUSY` or `HAL_EAGAIN`.

Only one application-visible response or event is queued per adapter. A
response to an incoming request remains in adapter-owned storage while the
underlying link is busy with its transport acknowledgement. Continue calling
`hal_lora_commands_process()` to retry it. The adapter and link use copied,
bounded buffers and keep caller ownership of the router and link handles.

Destroying an adapter returns `HAL_EBUSY` while processing or dispatch is
active, a response is pending, an application message remains unread, or the
underlying link has not returned to its receiving state. Continue processing
and consume queued messages before retrying destruction. An operation that has
already entered the API keeps its context alive until it returns, and a stale
handle cannot alias a later adapter.

An encrypted LoRa link supplies all command security flags; a plaintext link
supplies none. Handler policy can therefore require authenticated and
replay-protected delivery without depending on LoRa-specific metadata. The
full `hal_lora_link_message_info_t` view is available as the request source
context during dispatch.

`hal_lora_commands_get_info()` reports queue, link, processing and dispatch
state, while
`hal_lora_commands_get_diagnostics()` reports request, response, event,
protocol, dispatch, retry and drop counters.

## Authenticated BLE Stream adapter

```c
#include <hal/bluetooth/hal_ble_commands.h>
```

Initialize `hal_ble`, publish and provision `hal_ble_stream`, then attach one
command adapter. The application still owns controller polling and advertising:

```c
hal_ble_commands_config_t config = hal_ble_commands_config_defaults();
config.router = router;
config.initial_request_id = 1u;

hal_ble_commands_t commands = NULL;
hal_status_t status = hal_ble_commands_create(&config, &commands);

/* In the application loop, after controller servicing: */
status = hal_ble_poll();
if (status == HAL_OK || status == HAL_EOVERFLOW) {
  status = hal_ble_commands_process(commands);
}
```

The adapter is the sole command-wire consumer of the process-wide BLE Stream.
Do not call `hal_ble_stream_send()` or `hal_ble_stream_receive()` while it is
attached. The adapter does not own `hal_ble_poll()`, Stream initialization,
secret provisioning, advertising or the router. The current BLE transport is
a Peripheral with one Central peer; the command messages themselves remain
bidirectional and therefore need no adapter role or destination argument.

`hal_ble_commands_request_start()` and `hal_ble_commands_event_start()` copy a
complete message into bounded adapter storage. They require an authenticated
Stream session and return `HAL_EAUTH` otherwise. A successfully accepted
request receives a nonzero identifier immediately; repeated calls return
`HAL_EBUSY` while another wire message or automatic response owns the transmit
buffer. Continue calling `hal_ble_commands_process()` until all chunks have
entered the Stream queue.

One command-wire message may span many authenticated DATA payloads. The
adapter selects at most:

```text
min(HAL_BLE_STREAM_MAX_PAYLOAD, negotiated_ATT_MTU - 31)
```

bytes per chunk. The 31-byte allowance covers ATT, Stream framing, the
directional counter and the authentication tag. With an ATT MTU of at least
159, a 500-byte command body needs five chunks in each direction. The receiver
uses `hal_command_message_frame_size()` incrementally, preserves bytes after a
complete message and dispatches at most one request per process call.

Incoming requests are dispatched synchronously and answered automatically.
Responses and events are copied out with `hal_ble_commands_receive()`:

```c
hal_command_message_t message;
hal_ble_commands_peer_info_t peer;
if (hal_ble_commands_receive(commands, &message, &peer) == HAL_OK) {
  /* Match a response by request_id or consume an event by name. */
}
```

Every dispatched BLE request reports `HAL_COMMAND_SECURITY_ALL`, because only
payloads released by the mutually authenticated, encrypted, integrity-checked
and replay-protected Stream reach the adapter. `peer_id` losslessly contains
the address type and six BLE address bytes. `session_id` is the public random
identifier from the Stream handshake, represented as a little-endian 64-bit
value. During a handler call, `source_context` points to a borrowed
`hal_ble_commands_peer_info_t` with the address, connection, MTU, negotiated
capabilities, generations, session identifier and the first/last Stream DATA
counters used by the message.

A disconnect or new authenticated session clears incomplete receive data,
unfinished sends and unread messages from the previous peer session. A lost
Stream RX chunk, malformed command header, nonconsecutive chunk counter or
partial-frame timeout makes byte alignment unknowable, so the adapter closes
that Stream session instead of guessing a resynchronization point. The timeout
comes from `partial_frame_timeout_ms`; zero in the configuration selects
`HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS`.

`hal_ble_commands_process()` returns `HAL_OK` after progress and `HAL_EAGAIN`
when idle or waiting for Stream capacity/authentication. Protocol, timeout and
overflow failures are returned once and retained in diagnostics. Only one
application-visible response or event is queued. Destroying the adapter while
wire data, dispatch or an unread message remains returns `HAL_EBUSY`; a stale
handle returns `HAL_EUNINIT`.

## Network compatibility

`hal_net_commands` keeps its existing text/JSON, cJSON, HTTP and WebSocket API,
but its registrations and executions use the shared default router. Its legacy
handler type remains limited to direct, HTTP and WebSocket calls. Register a
generic `hal_command_definition_t` on the default router when the same handler
must accept both network and LoRa sources. Text execution passes the bytes
after the command name. JSON execution passes a compact serialization of the
`args` or `params` value. Network request, peer and session identifiers and
security flags are zero. The compatibility count and unregister operations act
on that shared handler set.

`hal_net_commands_clear()` clears the whole
default router and returns `hal_status_t`; it returns `HAL_EBUSY` and leaves
the set unchanged while any registered command, including one owned by
another adapter on the shared default router, is mid-dispatch.

The shared response keeps the established network-response fields in their
original order and appends its transport-neutral `encoding` field.

`HAL_ENABLE_BLE_STREAM` by itself remains a general authenticated byte stream
and does not enable the router. Select `HAL_ENABLE_BLE_COMMANDS` only when the
Stream payload is dedicated to command-wire traffic.

## Compile-time bounds

Define bounds before including HAL headers:

| Macro | Default | Valid range | Purpose |
|---|---:|---:|---|
| `HAL_COMMAND_ROUTER_MAX_INSTANCES` | 2 | 1..16 | Router pool, including the default router |
| `HAL_COMMAND_ROUTER_MAX_COMMANDS` | 8 | 1..64 | Handler slots per router |
| `HAL_COMMAND_ROUTER_NAME_MAX` | 32 | 2..256 | Command-name storage including the terminator |
| `HAL_COMMAND_RESPONSE_BUFFER_SIZE` | 512 | 32..65535 | Handler response storage |
| `HAL_COMMAND_MESSAGE_MAX_PAYLOAD` | 512 | 1..65535 | Owned wire payload storage |
| `HAL_BLE_COMMANDS_PARTIAL_FRAME_TIMEOUT_MS` | 5000 | greater than zero | Maximum lifetime of an incomplete BLE command-wire message |

The legacy net-command size macros remain aliases of the corresponding shared
router limits and must match them when both spellings are defined.
