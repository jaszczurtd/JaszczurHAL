# 21 - Natywna obsługa FDCAN w STM32G474

Jest to przykład CAN FD przeznaczony wyłącznie dla STM32G474. Korzysta z
peryferium FDCAN1 włączanego przez `HAL_ENABLE_STM32G474_FDCAN`.

Działanie:

- konfiguruje FDCAN1 na PA11/PA12;
- włącza CAN FD z arbitrażem 500 kbit/s i fazą danych 2 Mbit/s;
- co sekundę wysyła ramkę kontrolną CAN FD o identyfikatorze `0x123`;
- cyklicznie sprawdza kolejkę FIFO0 RX i wypisuje odebrane ramki przez port
  szeregowy.

## Połączenia

- PA11: FDCAN1_RX
- PA12: FDCAN1_TX

Połącz PA11/PA12 z transceiverem obsługującym CAN FD, nigdy bezpośrednio z
magistralą. Użyj wspólnej masy i zwykłej terminacji CAN, zazwyczaj po 120 omów
na obu końcach magistrali.

## Kompilacja

```bash
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
