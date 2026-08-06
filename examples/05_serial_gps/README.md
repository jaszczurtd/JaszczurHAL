# 05 - Serial and GPS

This example combines GPS parsing with an independent serial echo path.

The default application uses hardware UART port 1 for a 9600-baud GPS. On RP
targets it also uses hardware UART port 2 for a 115200-baud echo: the GPS uses
RX/TX GPIO 1/0 and the echo uses GPIO 5/4. On STM32G474 the GPS uses USART1 on
PA10/PA9; USART2 on PA3/PA2 remains exclusively owned by the debug/ST-Link VCP,
so the second echo is intentionally RP-only.

The RP-only `swserial` variant uses software serial for both paths. Its GPS is
on RX/TX GPIO 5/4 and its independent loopback/echo port is on GPIO 9/8. Build
that variant with `EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1` so the GPS backend also
selects software serial.

On RP targets, wire each echo TX pin back to its matching RX pin to exercise
receive and transmit. A disconnected GPS or echo loop does not stop the other
service.
