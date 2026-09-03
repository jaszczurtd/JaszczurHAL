# 20 - IRsmallDecoder receiver

Initializes an infrared receiver decoder on a GPIO interrupt pin and prints
decoded NEC frames through the debug helper.

Default wiring:

| Target | Input |
| --- | --- |
| RP2040 | GP16 |
| STM32G474 | PB0 |

The example uses `hal_debug_init_default()` and the supported `deb`/`derr`
logging macros from `hal/serial/hal_serial.h`. Change
`HAL_IRSMALL_PROTOCOL_NEC` in `app.c` to select another
supported protocol.
