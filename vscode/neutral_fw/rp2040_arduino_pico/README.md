# Neutral RP2040 Arduino-Pico Firmware

`neutral_identity.ino` is used by `jh-vscode clear-identity`.

It intentionally does not include JaszczurHAL or any project headers. The build
must use only the selected Arduino-Pico core and FQBN, and must not pass custom
USB manufacturer/product build properties.

