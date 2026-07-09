# Fiesta Parity Checklist

This checklist was completed while switching Fiesta firmware modules to the
shared JaszczurHAL VS Code entry. The current shared workflow also includes the
target/board selector (`Project: Select board`, `Project: Select board (GUI)`);
board selection is stored in gitignored `.vscode/jaszczurhal.local.json`, not in
tracked Fiesta manifests.

## Scope

- `Fiesta/src/ECU`
- `Fiesta/src/Clocks`
- `Fiesta/src/OilAndSpeed`
- `Fiesta/src/Fiesta_clock`
- `Fiesta/src/Adjustometer`, if its VS Code firmware workflow is migrated

`Fiesta/src/SerialConfigurator` stays a Fiesta desktop tool. Its protocol,
vocabulary, and module token definitions are not part of the public
JaszczurHAL VS Code entry contract.

## Current Behavior To Preserve

- Per-module `.vscode/` workspaces keep working.
- `--project <path>` maps to one firmware module, never the whole Fiesta repo.
- Build, debug build, upload, UF2 upload, serial monitor, and Debug Probe monitor
  work for every migrated module.
- `launch.json` pre-launch tasks point at `Project: Build (Debug)`.
- USB product strings remain distinguishable:
  - `Jaszczur Fiesta ECU`
  - `Jaszczur Fiesta Clocks`
  - `Jaszczur Fiesta OilAndSpeed`
  - `Jaszczur Fiesta RTC Clock`
  - `Jaszczur Fiesta Adjustometer`
- `/dev/serial/by-id/usb-Jaszczur_Fiesta_*` names remain compatible with the
  existing workflow.
- Flashing refuses to target a different Fiesta module when several Pico boards
  are attached.
- `clear-identity --project Fiesta/src/ECU` cannot clear Clocks, OilAndSpeed,
  RTC Clock, or any unverified manually attached BOOTSEL disk.
- Existing manifest validation for Fiesta firmware remains in force.
- `SerialConfigurator` and `serial-configurator-cli` detect the same modules as
  before migration.
- Host tests for SerialConfigurator still pass.
- The SC protocol, `HELLO`, SC vocabulary, and module tokens remain private to
  Fiesta.
- The serial monitor preserves reconnect, DTR/RTS, HUPCL, lock handling, and
  behavior after USB reenumeration.

## Required Checks

Record the result for each module before and after migration:

```text
Module:
Board/FQBN:
Expected USB identity:
Expected /dev/serial/by-id pattern:
Build:
Build debug:
Upload serial:
Upload UF2:
Serial monitor reconnect:
Debug Probe monitor:
Cortex-Debug launch:
Wrong-module flash refusal:
Clear identity refusal for wrong module:
SerialConfigurator detection:
Notes:
```

## Migration Run - 2026-07-08

Connected hardware:

- ECU: `usb-Jaszczur_Fiesta_ECU_DE62A875579C612A-if00`
- Clocks: `usb-Jaszczur_Fiesta_Clocks_E6625887D3475937-if00`
- Adjustometer: `usb-Jaszczur_Fiesta_Adjustometer_E6614C309367892A-if00`

Modules without hardware during this run:

- OilAndSpeed
- Fiesta_clock / RTC Clock

Results:

- Build passed for ECU, Adjustometer, Clocks, OilAndSpeed, and Fiesta_clock.
- Debug build passed for ECU, Adjustometer, Clocks, OilAndSpeed, and
  Fiesta_clock; each run emitted the memory map overview for
  `.build/firmware.elf`.
- `refresh-intellisense` passed for all five modules and generated
  `.build/compile_commands_patched.json` plus `.vscode/c_cpp_properties.json`.
- Serial upload passed on real hardware for ECU, Adjustometer, and Clocks via
  `Project: Upload` / `jh-vscode upload --project <module>`.
- After removing the stale common VS Code wrappers, serial upload was repeated
  for ECU, Adjustometer, and Clocks; all three passed through the manifest gate,
  UF2 flashing, and memory map overview.
- Each serial upload used the Fiesta manifest gate and produced
  `.build/firmware.manifest.json` before flashing `.build/firmware.uf2`.
- USB identity after flashing remained:
  - `Jaszczur Fiesta ECU`
  - `Jaszczur Fiesta Adjustometer`
  - `Jaszczur Fiesta Clocks`
- `/dev/ttyACM*` numbers changed after reenumeration, but stable
  `/dev/serial/by-id/usb-Jaszczur_Fiesta_*` names stayed correct.
- Wrong-module flash refusal passed: ECU upload with the Clocks by-id port was
  rejected before build/upload.
- Clear-identity wrong-module refusal passed: ECU `clear-identity` with the
  Clocks by-id port was rejected before build/upload.
- Missing-hardware refusal passed for OilAndSpeed and Fiesta_clock: default
  upload reported the expected identity and did not fall back to any attached
  Fiesta module.
- SerialConfigurator build passed and CTest reported 18/18 passing tests.
- SerialConfigurator CLI `detect` found ECU and Clocks via HELLO before and
  after the final reflashes. Adjustometer remained out-of-scope for
  SerialConfigurator detection as before.
- SerialConfigurator GUI flash was not executed in this terminal run. The CLI
  exposes `reboot-bootloader` with manifest/artifact preflight, not a full
  end-to-end flash command equivalent to the GUI Flash action.
- Serial monitor smoke passed for ECU, Adjustometer, and Clocks by opening the
  expected by-id link through `jh-vscode monitor` under a timeout.
- BOOTSEL/UF2 upload for OilAndSpeed and Fiesta_clock was not flashed because
  those boards were not physically available.
- Final hardware identity check after the last flash:
  - `usb-Jaszczur_Fiesta_ECU_DE62A875579C612A-if00 -> ttyACM0`
  - `usb-Jaszczur_Fiesta_Adjustometer_E6614C309367892A-if00 -> ttyACM1`
  - `usb-Jaszczur_Fiesta_Clocks_E6625887D3475937-if00 -> ttyACM2`
- Stale VS Code wrapper script audit passed: no active references to removed
  firmware wrappers remain outside generated `.build` artifacts.

## Documentation Updates

After the migration, update Fiesta agent/context documentation that describes
build, upload, monitor, or debug entrypoints. In particular, verify:

- `Fiesta/src/ECU/doc/Fiesta-context-providers/project-context-provider.en.txt`
- `Fiesta/src/ECU/doc/Fiesta-context-providers/serial-configurator-context-provider.txt`
- any Fiesta `AGENTS.md` file that mentions local wrapper scripts as the source
  of truth for VS Code firmware workflow
