# Windows Runtime

The public CLI contract is shared with Linux and exposed through `entry/`.

`entry/jh-vscode.cmd` resolves Python and starts the same shared runtime as the
Unix launcher. Firmware builds resolve the bootstrap-recorded tools, use the
short managed CMake root, and retain project-local artifacts and patched
compile commands. Windows-specific serial, process ownership, locking, and
BOOTSEL implementations live behind the platform adapter.

The native adapter enumerates and normalizes COM ports through pyserial,
including `COM10+`, and exposes VID, PID, serial number, manufacturer, product,
interface, location, HWID, and description to the shared identity matcher.
For the generic Windows `usbser` driver it also reads the parent PnP
bus-reported product, because pyserial otherwise reports the driver vendor and
omits the USB product. When Windows cannot expose the device manufacturer, an
exact product plus configured VID and PID is the minimum verified fallback.
Persistent monitors publish a versioned marker in the user's temporary
directory and use a per-port release file as the cooperative control channel.
PID reuse and process start time are checked before any fallback termination.
Windows build locks use `msvcrt.locking`, so the operating system releases them
when a process exits. Busy-port diagnostics treat Windows access-denied errors
as lock failures and report a validated monitor-marker PID when the operating
system cannot enumerate the COM-port owner.

BOOTSEL discovery enumerates drive roots with `GetLogicalDriveStringsW`, reads
the label and filesystem with `GetVolumeInformationW`, and resolves the volume
GUID through `GetVolumeNameForVolumeMountPointW`. Only FAT/FAT32 volumes labeled
`RPI-RP2`, `RP2350`, or `RPI-RP2350` are candidates. The shared runtime uses the
GUID for the pre-touch snapshot and refuses automatic upload when more than one
new candidate appears. `--bootsel-volume` or user-local `bootselVolume` selects
one verified drive root or GUID explicitly.

UF2 upload validates block magic, payload sizes, sequence completeness, family
groups, the RP2350 absolute-ignore block, and merged OTA images whose global
sequence spans multiple family IDs before opening the destination. The Windows
copy path streams data without metadata, detects source changes and short
writes, flushes the file handle, and closes it before success. Access denial,
read-only media, drive removal, and write errors remain upload failures. Native
Windows tests exercise real volume enumeration and durable file-handle flush
behavior; the shared regression test also uploads a mixed-family merged OTA
fixture through the adapter boundary. Real-device COM-to-BOOTSEL upload smoke
has passed on RP2040 and RP2350. The shared runtime also resolves managed GNU
Arm GDB, OpenOCD, and its CMSIS-DAP/ST-Link target scripts through
`debug-tools`; an actual Cortex session still requires a separately connected
SWD probe.
