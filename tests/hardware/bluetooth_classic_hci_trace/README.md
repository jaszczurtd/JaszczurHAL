# Bluetooth Classic HCI trace

The full procedure and recorded diagnosis are maintained in the
[central hardware-fixture reference](../../../doc/api/en/03_build_tests.md#bluetooth-classic-raw-hci-inquiry-diagnostics).

This private hardware fixture captures raw BTstack HCI command and event
packets before the public Classic manager interprets them. It supports Pico W
RP2040 and Pico 2 W RP2350 ARM so the same application can compare controller
behaviour. Bluetooth addresses and non-inquiry payloads are redacted on the
serial console.

Build and upload one target, open its serial console, then run `SCAN`, wait for
the ten-second inquiry to finish, and run `INFO` followed by `DUMP`. Use
`SCAN30` for three consecutive inquiry cycles. Both scan commands reset the
trace first. `RESET` discards a trace without starting inquiry and `STOP`
cancels an active scan. `INFO` includes HCI transport counters and the measured
CYW43 gSPI clock.

The `JHHCI` records retain the raw Inquiry command, Inquiry Complete, Inquiry
Result, and Inquiry Result with RSSI bytes. For Extended Inquiry Result they
retain only metadata through RSSI; the EIR data body is redacted because it can
contain names and arbitrary manufacturer data. Bluetooth addresses are always
masked. `JHHCI-PEER` reports both the advertised name-field length and its
NUL-terminated text length plus an FNV-1a hash, allowing padding and identity
diagnosis without printing the name. Other HCI commands and events retain only
the non-sensitive header needed to compare the command/event sequence.
