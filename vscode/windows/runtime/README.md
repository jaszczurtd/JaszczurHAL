# Windows Runtime

The public CLI contract is shared with Linux and exposed through `entry/`.

`entry/jh-vscode.cmd` resolves Python and starts the same shared runtime as the
Unix launcher. Firmware builds resolve the bootstrap-recorded tools, use the
short managed CMake root, and retain project-local artifacts and patched
compile commands. Windows-specific serial, process ownership, locking, and
BOOTSEL implementations live behind the platform adapter. Operations that
reach an unfinished device hook report exit code 8.
