# Shared WireGuard/lwIP engine

This directory contains the private WireGuard protocol engine used by
JaszczurHAL host-lwIP network backends. It is not a separately discoverable
library and does not expose Pico SDK, RP2040, CYW43, or target network types.

## Provenance

The protocol sources originate from the WireGuard-ESP32 port by Kenta Ida and
the later RP2040 adaptation by Marcin Kielesiński. Copyright,
attribution, and redistribution terms are preserved in `LICENSE` and in the
individual source headers.

The JaszczurHAL integration replaced the target-specific wrapper with:

- `wireguard_port.h`, which maps logging and timing helpers to public HAL APIs;
- `wireguard-platform.c`, which obtains entropy, monotonic time, TAI64N, and
  lwIP context through `jh_lwip_extension`;
- `jh_wireguard_client.cpp`, which owns the private lwIP netif, peer, route,
  timer, and teardown lifecycle using byte-array IPv4 values;
- the public, target-neutral facade in `hal/network/wireguard/hal_wireguard.h`.

## Integration contract

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
