<a id="21---natywna-obsługa-fdcan-w-stm32g474"></a>

# 21 - CAN FD z wbudowanym kontrolerem STM32G474

Przykład wysyła i odbiera ramki CAN FD przez wbudowany kontroler FDCAN1
w STM32G474. Co sekundę nadaje ramkę o identyfikatorze `0x123`, a odebrane
ramki odczytuje z FIFO0 i wypisuje w konsoli szeregowej.

Konfiguracja używa 500 kbit/s w fazie arbitrażu i 2 Mbit/s w fazie danych.
Kontroler jest włączany flagą `HAL_ENABLE_STM32G474_FDCAN`; przykład jest
przeznaczony wyłącznie dla STM32G474.

## Połączenia

PA11 to `FDCAN1_RX`, a PA12 to `FDCAN1_TX`. Podłącz je do transceivera
obsługującego CAN FD, nigdy bezpośrednio do magistrali CAN.
Połącz masy urządzeń i zastosuj terminację magistrali - zazwyczaj rezystor
120 Ω na każdym z jej dwóch końców.

## Kompilacja

Uruchom z katalogu tego przykładu:

```bash
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
