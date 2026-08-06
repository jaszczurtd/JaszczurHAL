# 21 - STM32G474 native FDCAN

STM32G474-only CAN FD example using the native FDCAN1 peripheral through
`HAL_ENABLE_STM32G474_FDCAN`.

What it does:
- configures FDCAN1 on PA11/PA12
- enables CAN FD with 500 kbit/s arbitration and 2 Mbit/s data phase
- sends one CAN FD heartbeat frame per second with CAN ID `0x123`
- polls RX FIFO0 and prints received frames to serial output

## Wiring

- PA11: FDCAN1_RX
- PA12: FDCAN1_TX

Connect PA11/PA12 to a CAN FD-capable transceiver, not directly to the bus.
Use a common ground and normal CAN bus termination, typically 120 ohm at each
end of the bus.

## Build

```bash
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
