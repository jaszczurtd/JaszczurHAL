<a id="21---stm32g474-native-fdcan"></a>

# 21 - CAN FD with the STM32G474 built-in controller

This example sends and receives CAN FD frames through the STM32G474 FDCAN1
peripheral. It transmits a frame with ID `0x123` once per second, reads
incoming frames from RX FIFO0, and prints them to the serial console.

The arbitration rate is 500 kbit/s and the data-phase rate is 2 Mbit/s.
`HAL_ENABLE_STM32G474_FDCAN` enables the controller. This example is for
STM32G474 only.

## Wiring

PA11 is `FDCAN1_RX` and PA12 is `FDCAN1_TX`. Connect them to a CAN FD-capable
transceiver, never directly to the CAN bus. Connect the devices' grounds and
terminate the bus, typically with a 120 Ω resistor at each of its two ends.

## Build

Run from this example's directory:

```bash
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
