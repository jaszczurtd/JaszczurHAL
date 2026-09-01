# RP KV power-loss hardware probe

See the
[central hardware-fixture reference](../../../doc/api/en/03_build_tests.md#rp-kv-power-loss-hardware-probe)
for build, upload, verification, and recorded physical-run details.

This fixture writes controlled incomplete KV banks to native RP flash after
erase, body program, and body verification. It reloads the EEPROM mirror from
physical flash to model a reboot, verifies fallback to the previous complete
bank, and verifies recovery of a fully published newer bank after a late
reported error. The build-only fault-injection switch must never be enabled in
production firmware.
