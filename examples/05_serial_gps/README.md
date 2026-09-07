<a id="05---serial-and-gps"></a>

# 05 - GPS reception and serial-port testing

This example reads GPS data and services a separate serial port for transmit
and receive tests. A disconnected GPS receiver or test-port loop does not
stop the other service.

The base application uses hardware UARTs. The GPS runs at 9600 baud and the
test port at 115200 baud.

| Target | GPS: port, RX / TX | Test port: port, RX / TX |
|---|---|---|
| RP family | UART 1, GP1 / GP0 | UART 2, GP5 / GP4 |
| STM32G474 | USART1, PA10 / PA9 | Not available; USART2 on PA3 / PA2 is reserved for the ST-Link VCP debug console. |

The RP-only `swserial` variant implements both serial ports in software.
It uses GP5/GP4 for GPS RX/TX and GP9/GP8 for test-port RX/TX.
Its build configuration sets `EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1` so the GPS
module also uses software serial.

For an RP loopback test, connect the test port's TX pin to its RX pin.
Do not make this connection on the port attached to the GPS receiver.
