<a id="20---irsmalldecoder-receiver"></a>

# 20 - Receiving infrared remote-control commands

This example reads an infrared receiver connected to a GPIO pin, decodes NEC
frames, and prints them to the debug console. Input edges are handled by a
GPIO interrupt.

| Target | Input |
| --- | --- |
| RP2040 | GP16 |
| STM32G474 | PB0 |

`hal_debug_init_default()` initializes the console. The `deb` and `derr`
macros from `hal/serial/hal_serial.h` print data and errors.
To select another supported protocol, replace `HAL_IRSMALL_PROTOCOL_NEC`
in the decoder configuration in `app.c`.
