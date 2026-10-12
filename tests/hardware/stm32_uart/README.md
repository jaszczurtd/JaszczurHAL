# STM32G474 hardware UART test

`tests/hardware/stm32_uart` checks `hal_uart` on a NUCLEO-G474RE: PORT_2
(USART2 on PA2/PA3), which reaches the host through the ST-LINK virtual COM
port, and PORT_1 (USART1) through a single-wire self-test on PC4 that needs no
wire.

`device_tree.json` describes the wiring of both assemblies, and the table
below repeats it. `nucleo-canhat` (the default) is the Nucleo with the CAN-FD
HAT: 160 MHz clock tree, the HAT's RDY LED (PC1) as the built-in LED and the
relay drivers held off. `nucleo` is the bare Nucleo: 170 MHz clock tree and
LD2 (PA5). The `nucleo` image leaves the relay lines alone, so use
`nucleo-canhat` whenever the HAT is fitted.

| Port | Bus in `device_tree.json` | Pins | Setting |
|---|---|---|---|
| PORT_2 (USART2) | `consoleUart` | TX PA2, RX PA3 (ST-LINK virtual COM port) | 3 Mbaud 8N1 after the boot window |
| PORT_1 (USART1) | `loopUart` | TX PC4 (the single wire), RX PC5 (unused) | rate and format of each `Q` command |

After reset the debug console prints a boot line at 115200 for three seconds:
reset reason, serial number and UID. Then the fixture takes USART2 at
3 Mbaud 8N1 and answers line commands from the verifier. The built-in LED
blinks while the boot line repeats and toggles on every command.

The verifier checks:

- the serial number (12 bytes, 24 hex digits) and UID, also against the UID
  words read through the debugger when given;
- echo of 64 KiB and 1 MB of random data at 3 Mbaud, using bulk reads and
  all-or-nothing non-blocking writes;
- 200 KB through `hal_uart_try_write_ex()` and 50 KB through blocking
  `hal_uart_write_ex()`, each at line rate;
- overrun: 2000 bytes sent while the fixture does not read; the newest 256
  bytes must stay readable in order and the other 1744 must be counted;
- framing errors (bytes at 1 Mbaud) and parity errors (odd parity into 8E1)
  in the error counters;
- frame formats 8E1, 8O1, 8N2, 8E2, 7E1 and 7O1 with echo; 5N1 and 6N1 must be
  refused while the port keeps running. 7N1, 6E1 and 6O1 are tried too but do
  not fail the run: the ST-LINK bridge does not carry them, the USART1
  self-test covers them;
- 1 Mbaud, 9600 baud and 1200 baud (the USART prescaler) with echo; 20 Mbaud
  must be refused;
- USART1 in single-wire mode (a register change made by the fixture joins TX
  and RX inside the USART; the HAL does the rest): 4 KiB at 3 Mbaud in 8N1,
  8E1, 8O1, 8E2, 7N1, 7E1, 6E1 and 6O1, at 1 Mbaud, and 48 bytes at 1200 baud,
  without errors;
- receive interrupts held off: the fixture masks interrupts for 40 ms while
  the host sends 300 to 4000 bytes. What comes back must be every byte, or the
  newest bytes with the rest counted in `rx_buffer_overflow`, or the newest
  bytes with an `rx_overrun` event; a 2 ms mask must raise no event;
- console output (`hal_deb`, `printf`) must not reach the line while the port
  is open, and must come back at 115200 after `hal_uart_destroy()`;
- unless `--skip-reset` is given (on the HAT every reset clicks the relays):
  `hal_system_reset()`, after which the next boot reports `SOFT`, and a main
  stack overflow into the stack guard while the port is open: no text on the
  line, the next boot reports `STACK_OVERFLOW`.

A FreeRTOS build adds a two-task check: one task sends with blocking writes
while `app_task1` reads what the host sends and probes the writer lock with
zero-length non-blocking writes, at 1 Mbaud and 3 Mbaud. Both streams must
arrive intact, the probe must be refused while the writer holds the queue and
must return within a scheduler tick.

Build and upload through the ST-LINK (OpenOCD); use `--assembly nucleo`
for a Nucleo without the HAT:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/stm32_uart \
  --assembly nucleo-canhat
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_uart \
  --assembly nucleo-canhat
```

The `FREERTOS` variant (`HAL_ENABLE_FREERTOS=1`, `HAL_ENABLE_APP_TASK1=1`)
is the FreeRTOS build. Add `--variant FREERTOS` to both commands:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_uart \
  --assembly nucleo-canhat --variant FREERTOS
```

Optionally read the UID words without resetting the board:

```sh
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c init -c "mdw 0x1fff7590 3" -c exit
```

Run the verifier; pass the three words as 24 hex digits to compare them with
the reported serial number:

```sh
python3 tests/hardware/stm32_uart/verify_stm32_uart.py \
  --port /dev/serial/by-id/<st-link virtual com port> \
  --expect-serial <word0><word1><word2>
```

It finds the fixture in command mode or waits for its boot line (press the
reset button if the board booted long ago and does not answer). It prints a
line per check (`INFO` for the checks that do not fail the run) and a JSON
summary; `--json <file>` keeps every result. Exit code 0 means pass, 1 fail.
