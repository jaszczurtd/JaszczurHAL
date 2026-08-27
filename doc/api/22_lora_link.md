# Reliable LoRa link API

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

`hal_lora_link` is a small private point-to-point messaging layer above one
configured [`hal_lora_radio`](21_lora.md) handle. It adds 16-bit addressing,
32-bit message sequences, acknowledgements, bounded whole-message retries,
duplicate suppression and transparent fragmentation. Optional
ChaCha20-Poly1305 gives every data fragment confidentiality and authentication,
and authenticates acknowledgements.

This protocol is specific to JaszczurHAL. It is not LoRaWAN, LoRa Alliance
certified, routable, or interoperable with LoRaWAN gateways. Applications
remain responsible for legal frequency, power, airtime and duty cycle.

## Enable the module

Select the link and exactly one raw-radio provider:

```c
#pragma once

#define HAL_ENABLE_SX126X
#define HAL_ENABLE_LORA_LINK
```

`HAL_ENABLE_LORA_LINK` propagates `HAL_ENABLE_LORA` and `HAL_ENABLE_CRC`.
The selected SX126x or SX127x provider propagates `HAL_ENABLE_SPI`. Define
`HAL_ENABLE_CRYPTO` as well when using
`HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305`.

The following compile-time bounds are available before `hal_config.h` is
included:

| Macro | Default | Valid range | Purpose |
|---|---:|---:|---|
| `HAL_LORA_LINK_MAX_INSTANCES` | 2 | 1..255 | Generation-tagged static link slots |
| `HAL_LORA_LINK_MAX_MESSAGE_SIZE` | 1024 | 1..4096 | Per-link copied TX and RX message buffers |
| `HAL_LORA_LINK_MAX_PEERS` | 8 | 1..32 | Source/session duplicate windows retained per link |

Each link also owns two 255-byte frame work buffers. No protocol operation
allocates from the heap after the per-handle mutex has been created.

## Lifecycle

Create and configure the raw radio first, then attach a link:

```c
hal_lora_link_t link = NULL;
hal_lora_link_config_t config =
    hal_lora_link_config_defaults(radio, UINT16_C(0x1001), session_id);

hal_status_t status = hal_lora_link_create(&config, &link);
if (status != HAL_OK) {
  return status;
}
```

Local address zero is reserved, and `0xFFFF` is the broadcast destination.
The nonzero session ID distinguishes restarts of one address. It must be new
for every address/key session. Use a cryptographically random value or a
persistent monotonic boot counter; never derive it only from a predictable
uptime clock when encryption is enabled.

The link takes exclusive operational ownership of the radio, clears its raw
event callback and starts continuous receive. The caller must keep the radio
alive but must not issue raw TX, RX, CAD, sleep or calibration calls until
`hal_lora_link_destroy()` returns. Destroying the link cancels active radio I/O,
zeroizes its copied key and leaves the radio in standby; it does not destroy
the radio handle.

Opaque link handles are generation-tagged. A stale handle returns
`HAL_EUNINIT`, and creating more than `HAL_LORA_LINK_MAX_INSTANCES` links
returns `HAL_ENOMEM`.

## Sending and receiving

`hal_lora_link_send_start()` copies the complete message, starts the first
fragment and returns. Call `hal_lora_link_process()` regularly from one main
loop or owning FreeRTOS task:

```c
static const uint8_t message[] = "acknowledged telemetry";

status = hal_lora_link_send_start(link, UINT16_C(0x1002), 3u, message,
                                  sizeof(message) - 1u, true);
while (status == HAL_OK || status == HAL_EAGAIN || status == HAL_IGNORED) {
  status = hal_lora_link_process(link);

  hal_lora_link_send_status_t send;
  if (hal_lora_link_get_send_status(link, &send) == HAL_OK &&
      send.state != HAL_LORA_OPERATION_IN_PROGRESS) {
    break;
  }
}
```

The application-defined port is carried unchanged. Unicast can be acknowledged
or unacknowledged; broadcast must be unacknowledged. Only one application send
and one completed receive message may be retained by a link at a time.

`hal_lora_link_receive()` copies and consumes the queued complete message.
`HAL_EAGAIN` means no message is ready. If the caller's buffer is too small it
returns `HAL_EOVERFLOW`, reports the required length and still consumes the
message.

```c
uint8_t buffer[HAL_LORA_LINK_MAX_MESSAGE_SIZE];
size_t length = 0u;
hal_lora_link_message_info_t info;

status = hal_lora_link_receive(link, buffer, sizeof(buffer), &length, &info);
if (status == HAL_OK) {
  /* info contains source, destination, session, sequence, port and RF data. */
}
```

`hal_lora_link_cancel()` stops only an active application send and resumes
continuous receive. State, send-status and diagnostics snapshots are protected
by the handle mutex and may be queried from another task. The complete
`process()` state machine must still have one logical owner.

## Command adapter

`HAL_ENABLE_LORA_COMMANDS` adds the
[`hal_lora_commands`](23_commands.md#reliable-lora-adapter) adapter and
propagates both `HAL_ENABLE_COMMAND_ROUTER` and `HAL_ENABLE_LORA_LINK`. It
encodes bounded request, response and event messages on one application-defined
link port:

```c
hal_lora_commands_config_t commands_config =
    hal_lora_commands_config_defaults(link, 7u);
hal_lora_commands_t commands = NULL;

status = hal_lora_commands_create(&commands_config, &commands);
if (status == HAL_OK) {
  uint32_t request_id = 0u;
  status = hal_lora_commands_request_start(
      commands, UINT16_C(0x1002), "status", HAL_COMMAND_ENCODING_TEXT,
      NULL, 0u, &request_id);
}
```

The link must already be receiving when attached. The adapter then becomes its
sole processing and receive owner: call `hal_lora_commands_process()` instead
of the two corresponding link functions. Incoming requests are dispatched
synchronously through the configured router and responses are sent
automatically. Application-visible responses and events are consumed through
`hal_lora_commands_receive()`.

Plaintext links report no command security flags. A link using
`HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305` reports authenticated, encrypted,
integrity-protected and replay-protected delivery, so router policies can
reject less protected requests. The handler receives the source address,
session and complete link metadata without coupling its command logic to the
radio provider.

## Reliability and fragmentation

The default policy waits 1500 ms for one acknowledgement after the complete
message, backs off 200 ms and retries the whole immutable message up to three
times. Configuration can bound the acknowledgement timeout, retry count,
backoff and incomplete-reassembly lifetime. `attempts` includes the first
transmission and is wide enough to report all 256 attempts allowed by the
8-bit `max_retries` field. Exhaustion finishes the send with `HAL_ETIMEOUT`.

A 25-byte versioned header leaves 230 bytes for an unprotected fragment or 214
bytes when the 16-byte AEAD tag is present. Messages are split into at most 32
fragments. The receiver validates the declared shape and reassembles only a
matching source, destination, session, sequence, port and message identity.
Plaintext messages carry one CRC-32 over the complete message; this detects
accidental corruption but is not authentication.

After complete reassembly, the receiver records the source/session sequence in
a 32-message sliding window. A retried message is not delivered twice, but its
last fragment triggers another acknowledgement so a lost ACK can recover.
Windows older than the configured peer table are evicted by least-recent use.

## Optional cryptographic protection

With `HAL_ENABLE_CRYPTO`, install one 32-byte pre-shared key and select AEAD:

```c
uint8_t provisioned_key[HAL_LORA_LINK_CRYPTO_KEY_BYTES];
/* Load a secret from protected provisioning or storage. */

hal_lora_link_config_t config =
    hal_lora_link_config_defaults(radio, local_address, fresh_session_id);
config.security = HAL_LORA_LINK_SECURITY_CHACHA20_POLY1305;
config.key = provisioned_key;
config.key_length = sizeof(provisioned_key);
status = hal_lora_link_create(&config, &link);
```

The key is copied into link-owned storage. The nonce combines sender session,
source address, sequence, fragment index and frame type. Consequently the
same key/address/session tuple must never be reused, and an encrypted link
refuses to send after its 32-bit sequence space is exhausted. Retransmissions
reuse the exact message identity and therefore the same authenticated frame;
they never encrypt different plaintext under that nonce.

AEAD authenticates the full header, ciphertext and acknowledgements. It does
not hide addresses, session IDs, sequences, sizes or fragment counts. Key
provisioning, rotation and persistent session management remain application
responsibilities. A plaintext link rejects encrypted frames, and an encrypted
link rejects plaintext or unauthenticated frames.

## Diagnostics and example

`hal_lora_link_get_diagnostics()` reports message/frame totals, ACKs,
retransmissions, duplicates, malformed/authentication/integrity failures,
reassembly drops/timeouts, queue drops, send timeouts, cancellation and recent
address/RF observations.

`examples/27_lora_point_to_point` provides `link` and `link-responder`
variants. They exchange a correlated 500-byte command request and response
through `hal_lora_commands`, forcing three fragments in each direction over
the same SX1262 fixtures used by the raw-radio example. The example uses
plaintext intentionally; a product should enable AEAD only after it has a real
key-provisioning and unique-session strategy.
