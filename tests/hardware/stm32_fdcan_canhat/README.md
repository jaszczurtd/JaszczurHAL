# STM32G474 CAN-FD HAT hardware test

`tests/hardware/stm32_fdcan_canhat` validates the native FDCAN backend on a
NUCLEO-G474RE with the [Embedded Garage](https://www.youtube.com/@embeddedGarage)
CAN-FD HAT v1.2 (`nucleo-g474re-canhat` board profile). After reset the
fixture runs every check once, then prints the report every two seconds:
`JHCANHAT begin` with the totals, one `JHCANHAT <name> <PASS|FAIL|SKIP>
<detail>` line per check, and `JHCANHAT end`. The BUSY LED (PC2) shines while
the checks run; afterwards the RDY LED (PC1, the board's built-in LED) blinks
at 1 Hz when nothing failed and at 5 Hz otherwise.

Without any wiring the fixture checks:

- the board profile (three CAN channels) and the clock tree: PLL from the
  24 MHz HSE, FDCAN kernel clock 80 MHz from PLL Q, UCPD dead-battery pull-downs
  off;
- internal and external loopback of classic frames at 125k, 500k and 1M, and
  of CAN FD frames at 500k/2M, 500k/4M and 1M/5M, on FDCAN1, plus 500k/2M on
  FDCAN2 and FDCAN3;
- three channels at once, each receiving only its own frames, and no fourth
  board channel;
- a 40-frame burst through the send queue with exact delivery and overflow
  counts, and receive callbacks;
- send timestamps 224 µs apart at 500 kbit/s;
- filters: 28 standard and 8 extended elements, the first matching filter
  decides, adding and removing filters while frames flow without stopping the
  controller, remote frames refused only on a stopped channel;
- a lone node: retransmission ends in bus-off, one-shot sends fail one by one,
  manual recovery waits for `hal_can_recover()` (also after a blocking send,
  which returns `HAL_EBUS`), stopping ends queued frames;
- mode changes: frames waiting in listen-only end when the channel switches
  to classic CAN instead of going out as FD frames, and a channel whose mode
  has no FD refuses FD frames;
- a classic filter slot that moves to the full extended list keeps its old
  filter.

The `shared_bus` check needs CN5, CN6 and CN7 joined into one bus (CAN-H,
ground and CAN-L through all three connectors) with the termination jumpers of
the two outer channels fitted, e.g. H1 and H3; CAN-H to CAN-L measures about
60 Ω without power. Each channel then sends a classic and an FD frame that the
other two must receive. Without the bus nobody acknowledges and the check
reports SKIP. If the bus is wired and the check still skips, check the 5 V
supply of the MCP2562FD transceivers first.

Build and upload through the ST-LINK (OpenOCD):

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/stm32_fdcan_canhat \
  --target stm32g474 --board nucleo-g474re-canhat
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_fdcan_canhat \
  --target stm32g474 --board nucleo-g474re-canhat
```

Run the verifier (press the reset button to repeat the checks):

```sh
python3 tests/hardware/stm32_fdcan_canhat/verify_stm32_fdcan_canhat.py \
  --port /dev/serial/by-id/<st-link virtual com port>
```

The ST-LINK may still hold reports of the image that ran before the upload,
so the verifier waits for half a second of silence before it takes a report.
It prints the report as JSON and passes when no check failed; `--require-bus`
also fails a skipped `shared_bus` check, for runs with the bus wired. Exit
code 0 means pass, 1 fail, 2 no complete report before `--timeout` (30 s by
default).
