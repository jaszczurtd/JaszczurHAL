# Fiesta Parity Checklist

This checklist must be completed before any Fiesta firmware module is switched
to the shared JaszczurHAL VS Code entry.

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
  - any existing Adjustometer identity, if currently emitted
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

## Documentation Updates

After the migration, update Fiesta agent/context documentation that describes
build, upload, monitor, or debug entrypoints. In particular, verify:

- `Fiesta/src/ECU/doc/Fiesta-context-providers/project-context-provider.en.txt`
- `Fiesta/src/ECU/doc/Fiesta-context-providers/serial-configurator-context-provider.txt`
- any Fiesta `AGENTS.md` file that mentions local wrapper scripts as the source
  of truth for VS Code firmware workflow

