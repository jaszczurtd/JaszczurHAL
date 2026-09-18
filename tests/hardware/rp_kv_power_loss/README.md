# RP KV power-loss hardware test

`tests/hardware/rp_kv_power_loss` enables a build-only fault-injection hook in
the native flash provider. It interrupts inactive-bank replacement after
invalidation, body programming, body verification, or publication. After each
case the fixture reloads the EEPROM mirror from physical flash, just as a new
boot would, and asks shared `hal_kv` to select a bank. The first three cases
must recover the previous value; the late error after complete publication
must recover the new value. The fixture also checks a deferred two-key commit
and the read-through regression. Read-through getters must return `HAL_EBUSY`
and zero their scalar output or blob length while the RAM image contains
unpublished changes. After a commit and after reloading from physical flash,
they must return the newly published scalar and blob.

The probe erases and owns the complete native EEPROM/KV reservation. Do not run
it on a board whose persistent tail must be preserved. The fault-injection
define is fixture-only and must not be used by application firmware.

Build and upload with the regular workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_kv_power_loss \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_kv_power_loss \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Run the verifier:

```sh
python3 tests/hardware/rp_kv_power_loss/verify_kv_power_loss.py \
  --port /dev/serial/by-id/<device> --target rp2040
```

Use `--target rp2350-arm --board pico2` for Pico 2 and pass the same target to
the verifier.
