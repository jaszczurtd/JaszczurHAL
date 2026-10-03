<a id="21---stm32g474-native-fdcan"></a>

# 21 - CAN FD with the STM32G474 built-in controller

This example runs every CAN channel the board profile describes through the
STM32G474 built-in FDCAN controllers. Its default board is
`nucleo-g474re-canhat`, a NUCLEO-G474RE with the
[Embedded Garage](https://www.youtube.com/@embeddedGarage) CAN-FD HAT, which
has three channels (CN5, CN6, CN7) with MCP2562FD transceivers.

Each channel takes its settings from `hal_can_board_config()`: pins,
transceiver standby line, 500 kbit/s arbitration and 2 Mbit/s data phase.
Once per second every channel queues a 12-byte CAN FD frame with bitrate
switching (IDs `0x120`, `0x121`, `0x122`) with `hal_can_send_frame_ex()`.
`hal_can_service()` runs the callbacks: received frames are printed with their
timestamp, and sends that did not succeed are reported with the reason.
The built-in LED (RDY on the HAT) toggles after every second in which each
channel received a frame and stays off otherwise.

`HAL_ENABLE_STM32G474_FDCAN` enables the controller. This example is for
STM32G474 boards whose profile has CAN channels.

## Loopback or shared bus

By default every channel runs in internal loopback and receives its own
frames, so the example works on a HAT with nothing connected.

With `EXAMPLE_SHARED_BUS` set to 1 the channels run in normal mode and each
one receives the frames of the other two. Join CN5, CN6 and CN7 into one bus
(CAN-H, ground and CAN-L through all three connectors) and fit the termination
jumpers of the two outer channels, e.g. H1 and H3; CAN-H to CAN-L measures
about 60 Ω without power. Other nodes on that bus see the frames as well.

## Build

Run from this example's directory:

```bash
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
