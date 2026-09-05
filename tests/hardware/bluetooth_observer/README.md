# Bluetooth Observer hardware probe

The complete requirements, procedure, acceptance criteria, and recorded results
are maintained in the
[central hardware-fixture reference](../../../doc/api/en/03_build_tests.md#bluetooth-observer-hardware-probe).

The serial commands `STOP`, `START`, `REOPEN`, and `INFO` exercise scan
shutdown/startup, full BLE profile reacquisition without a controller reset,
and bounded diagnostics. A C10 regression run must receive another valid
report after both `START` and `REOPEN`.
