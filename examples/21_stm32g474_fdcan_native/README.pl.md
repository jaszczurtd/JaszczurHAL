<a id="21---natywna-obsługa-fdcan-w-stm32g474"></a>

# 21 - CAN FD z wbudowanym kontrolerem STM32G474

Przykład obsługuje wszystkie kanały CAN wybranego montażu przez wbudowane
kontrolery FDCAN w STM32G474. Jego `device_tree.json` ma jeden montaż,
`nucleo-canhat`: NUCLEO-G474RE z nakładką CAN-FD HAT od
[Embedded Garage](https://www.youtube.com/@embeddedGarage), która ma trzy
kanały (CN5, CN6, CN7) z transceiverami MCP2562FD. Plik opisuje też pozostałe
połączenia nakładki i wybiera drzewo zegara `hse-160mhz`: SYSCLK 160 MHz
i zegar FDCAN 80 MHz z kwarcu 24 MHz na płytce.

Każdy kanał bierze ustawienia z `hal_can_board_config()`: piny, linię standby
transceivera, 500 kbit/s w fazie arbitrażu i 2 Mbit/s w fazie danych. Co
sekundę każdy kanał kolejkuje funkcją `hal_can_send_frame_ex()` 12-bajtową
ramkę CAN FD z przełączaniem prędkości (identyfikatory `0x120`, `0x121`,
`0x122`). `hal_can_service()` wywołuje funkcje zwrotne: odebrane ramki są
wypisywane ze znacznikiem czasu, a nieudane nadania z przyczyną. Wbudowana
dioda (RDY na nakładce) zmienia stan po każdej sekundzie, w której każdy kanał
odebrał ramkę, a w przeciwnym razie pozostaje zgaszona.

Kontroler jest włączany flagą `HAL_ENABLE_STM32G474_FDCAN`. Przykład jest
przeznaczony dla montaży STM32G474 z kanałami CAN.

## Pętla zwrotna albo wspólna magistrala

Domyślnie każdy kanał pracuje w wewnętrznej pętli zwrotnej i odbiera własne
ramki, więc przykład działa na nakładce bez żadnych połączeń.

Przy `EXAMPLE_SHARED_BUS` ustawionym na 1 kanały pracują w trybie normalnym i
każdy odbiera ramki dwóch pozostałych. Połącz CN5, CN6 i CN7 w jedną
magistralę (CAN-H, masa i CAN-L przez wszystkie trzy złącza) i załóż zworki
terminacji dwóch skrajnych kanałów, np. H1 i H3; bez zasilania między CAN-H a
CAN-L powinno być ok. 60 Ω. Pozostałe węzły tej magistrali też widzą te
ramki.

## Kompilacja

Uruchom z katalogu tego przykładu:

```bash
../../vscode/entry/jh-vscode build --project . --target stm32g474
```
