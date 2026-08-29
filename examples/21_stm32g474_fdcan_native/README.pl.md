# 21 - Native FDCAN STM32G474

Przykład CAN FD wyłącznie dla STM32G474, używający peryferium FDCAN1 przez
`HAL_ENABLE_STM32G474_FDCAN`.

Działanie:
- konfiguruje FDCAN1 na PA11/PA12
- włącza CAN FD z arbitrażem 500 kbit/s i fazą danych 2 Mbit/s
- co sekundę wysyła ramkę heartbeat CAN FD z CAN ID `0x123`
- odpytuje FIFO0 RX i wypisuje odebrane ramki na wyjście serial

## Połączenia

- PA11: FDCAN1_RX
- PA12: FDCAN1_TX

Połącz PA11/PA12 z transceiverem obsługującym CAN FD, nigdy bezpośrednio z
magistralą. Użyj wspólnej masy i zwykłej terminacji CAN, zazwyczaj po 120 omów
na obu końcach magistrali.

## Build

```bash
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
