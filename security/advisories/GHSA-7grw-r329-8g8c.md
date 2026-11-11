# GHSA-7grw-r329-8g8c: WireGuard endpoint protection

## Upstream advisory

This summary follows the [wireguard-lwip advisory](https://github.com/smartalock/wireguard-lwip/security/advisories/GHSA-7grw-r329-8g8c),
published on 2026-11-10. It covers versions through `c54f20d`; upstream fixes
end at [`cfba916`](https://github.com/smartalock/wireguard-lwip/commit/cfba916).
The overall severity is Moderate, CVSS 3.1 **6.5**
(`CVSS:3.1/AV:N/AC:H/PR:N/UI:N/S:U/C:L/I:N/A:H`). No CVE is assigned.

Captured traffic can enable endpoint redirection without private keys,
disrupting connectivity and letting an attacker relay or drop encrypted
packets. This does not expose or alter their plaintext. The heap-read issue
requires an authenticated peer; recovering leftover secrets requires memory
access.

| Issue | Failure | CVSS 3.1 / severity | CWE |
|---|---|---|---|
| 1 | Transport replay changes the destination | 6.5 / Moderate | CWE-294 |
| 2 | Cookie forgery changes the destination | 6.5 / Moderate | CWE-345 |
| 3 | Truncated decrypted payload causes a heap overread | 2.7 / Low | CWE-125 |
| 4 | Restart permits old initiations | 3.7 / Low | CWE-294 |
| 5 | Ineffective initiation throttle | 2.2 / Low | CWE-697 |
| 6 | Stale session state survives expiry | Low | CWE-459 |
| 7 | Temporary secrets remain in memory | Low | CWE-226 |
| 8 | Wrong receive counter / duplicate session identifiers | Low | CWE-697 |

## JaszczurHAL handling

The bundled [engine](../../src/hal/network/wireguard/core/UPSTREAM.md) contains
adapted fixes for the shared lwIP implementation selected by
`HAL_ENABLE_WIREGUARD`. Its local provenance and version remain recorded in
[third_party.json](../third_party.json) and the generated
[SBOM](../sbom.cdx.json); this is a backport, not an upstream version change.

The receive path checks authentication and replay counters, including
keepalives, before changing the endpoint, timers, keypairs or link state.
Cookie replies only update the stored cookie. Inner IPv4 packets need at
least 20 bytes before header access; the engine drops inner IPv6 packets.
It checks the received counter against the message limit without destroying
the session on an unauthenticated counter, and checks all peer slots when
allocating session identifiers.

The peer tracks session creation independently of key expiry. After 540 s
without a new session, it clears keys and handshake state and restores the
configured endpoint. Previous and pending keypairs expire after 180 s.
The crypto routines wipe temporary key, hash and cipher state after use.

For initiation replay protection, the HAL client uses the synchronized clock
at `begin()` minus 120 s and retains the greatest accepted timestamp across
`end()`/`begin()` for the same peer key. The initial clock floor stays fixed;
a device reset clears the remembered timestamps. Applications must synchronize
the clock before starting WireGuard. See the
[API requirements](../../doc/api/en/15_connectivity.md).

An accepted initiation receives a response at its source address without
moving the saved endpoint. Separate responder state preserves the device's
own pending handshake. The endpoint can change after a fresh authenticated
transport packet or a valid response to the device's own initiation. These
rules also apply when the timestamp check accepts an old initiation.

### Remaining rate-limit boundary

The initiation throttle normally enforces a 500 ms interval. If an initiation
is accepted exactly when the 32-bit millisecond counter is zero, the next
valid initiation can pass before 500 ms because zero also denotes an empty
receive history. This rare startup/wrap boundary remains open; it does not
bypass transport authentication or the endpoint rules above.

The [vulnerability log](../vulnerability_log.md) records the backport decision.
The advisory has no package ecosystem mapping, so a clean dependency scan
alone does not establish whether the derived engine contains these fixes.
