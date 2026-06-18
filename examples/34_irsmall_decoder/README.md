# Example 34: IRsmallDecoder receiver

Initializes an infrared receiver decoder on a GPIO interrupt pin and prints
decoded NEC frames through the debug helper.

Default wiring:

| Target | Input |
| --- | --- |
| RP2040 | GP16 |
| STM32G474 | PB0 |

The example uses `debugInit()` and the `deb`/`derr` logging macros from
`tools_c.h`. Change `HAL_IRSMALL_PROTOCOL_NEC` in `app.c` to select another
supported protocol.
