<a id="05---serial-and-gps"></a>

# 05 - GPS reception and serial-port testing

This example reads GPS data and services a separate serial port for transmit
and receive tests. A disconnected GPS receiver or test-port loop does not
stop the other service.

The base application uses hardware UARTs. The GPS runs at 9600 baud and the
test port at 115200 baud. `device_tree.json` describes the wiring of every
assembly, and the program takes ports, pins and serial settings from the
generated header. The table below repeats that wiring.

| Assembly | GPS: port, RX / TX | Test port: port, RX / TX |
|---|---|---|
| Pico / Pico 2 | UART 1, GP1 / GP0 | UART 2, GP5 / GP4 |
| NUCLEO-G474RE | USART1, PA10 / PA9 | Not available; USART2 on PA3 / PA2 is reserved for the ST-Link VCP debug console. |
| ESP32-S3-Zero | UART 1, GPIO10 / GPIO9 | Not available |

The RP-only `SWSERIAL` variant implements both serial ports in software on
the same lines: GPS RX/TX on GP1/GP0 and the test port on GP5/GP4, at
9600 baud. Both builds therefore run on one unchanged setup.
Its build configuration sets `EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1` so the GPS
module also uses software serial.

For an RP loopback test, connect the test port's TX pin to its RX pin.
Do not make this connection on the port attached to the GPS receiver.
