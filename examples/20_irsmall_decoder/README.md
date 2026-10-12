<a id="20---irsmalldecoder-receiver"></a>

# 20 - Receiving infrared remote-control commands

This example reads an infrared receiver connected to a GPIO pin, decodes NEC
frames, and prints them to the debug console. Input edges are handled by a
GPIO interrupt. The driver enables the pull-up resistor on the input.

`device_tree.json` describes the wiring of every assembly, and the program
takes the receiver pin from the generated header. The table below repeats
that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| Receiver output | GP16 | PB0, CN7 pin 34 / A3 | GPIO16 |

`hal_debug_init_default()` initializes the console. The `deb` and `derr`
macros from `hal/serial/hal_serial.h` print data and errors.
To select another supported protocol, replace `HAL_IRSMALL_PROTOCOL_NEC`
in the decoder configuration in `app.c`.
