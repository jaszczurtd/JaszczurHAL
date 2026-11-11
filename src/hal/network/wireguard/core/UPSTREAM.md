# Shared WireGuard/lwIP engine

This directory contains the private WireGuard protocol engine used by
JaszczurHAL host-lwIP network backends. It is not a separately discoverable
library and does not expose Pico SDK, RP2040, CYW43, or target network types.

## Provenance

The protocol sources originate from the WireGuard-ESP32 port by Kenta Ida and
the later RP2040 adaptation by Marcin Kielesiński. Copyright,
attribution, and redistribution terms are preserved in `LICENSE` and in the
individual source headers.

The engine includes the security fixes of
[wireguard-lwip](https://github.com/smartalock/wireguard-lwip) up to commit
`cfba916` (advisory GHSA-7grw-r329-8g8c), with these differences:

- the client seeds the greatest handshake timestamp of the peer from the
  synchronized clock and keeps it across `end()`/`begin()`, instead of the
  upstream callback for saving it;
- a handshake initiation does not move the peer endpoint: the response goes to
  its source address and the endpoint follows authenticated transport packets
  and the response to the device's own initiation, so a replayed initiation
  that the timestamp check lets through (after a device reset, or one that
  never arrived) cannot redirect the tunnel;
- a received initiation is handled in a separate responder state of the
  device, so neither such a replay nor a real initiation of the peer that
  crosses ours cancels the initiation this device has in flight;
- a peer reset after 540 s depends on a session flag rather than on a zero
  timestamp, so a session created when the millisecond counter reads 0 is not
  reset at once;
- inner packets other than IPv4 are always dropped, also where lwIP is built
  with IPv6;
- the HChaCha20 state is wiped as well.

The JaszczurHAL integration replaced the target-specific wrapper with:

- `wireguard_port.h`, which maps logging and timing helpers to public HAL APIs;
- `wireguard-platform.c`, which obtains entropy, monotonic time, TAI64N, and
  lwIP context through `jh_lwip_extension`;
- `jh_wireguard_client.cpp`, which owns the private lwIP netif, peer, route,
  timer, and teardown lifecycle using byte-array IPv4 values;
- the public, target-neutral facade in `hal/network/wireguard/hal_wireguard.h`.

## Integration requirements

The selected network backend must advertise resolver, UDP, host-stack L3,
virtual-netif/route, stack-context, and cryptographic-entropy capabilities. The
backend supplies an underlay lwIP netif and serialized stack entry/leave
operations. Unsupported socket-offload backends are rejected at compile time.

Applications synchronize wall-clock time before starting WireGuard, then use
the regular public UDP/TCP/MQTT APIs. Split and full-tunnel routing are
installed at lwIP netif level; consumers do not contain WireGuard-specific
branches.

`end()` removes the peer, cancels its lwIP timer, removes the UDP PCB and
virtual netif, and restores the previous default route. The facade is a
singleton because the bundled engine is configured for one peer and one
private interface.

## Current target coverage

- RP2040/RP2350 through the shared JaszczurHAL lwIP extension;
- STM32G474 with the shared CYW43/lwIP backend and hardware RNG entropy.

The shared sources remain IPv4-only. Outer and inner IPv6 support require
separate future routing and traffic gates.
