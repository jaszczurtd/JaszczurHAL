<a id="27---łącze-lora-punkt-punkt"></a>

# 27 - Wymiana pakietów i poleceń przez LoRa

Przykład pozwala połączyć dwa urządzenia z radiem SX1262. W wersji podstawowej
jedno wysyła pakiet, a drugie odpowiada - zbuduj je odpowiednio jako
inicjator i wariant `RESPONDER`. Obsługa nadawania i odbioru jest
asynchroniczna, wykorzystuje DIO1 i funkcje zwrotne. Kod pokazuje też
anulowanie operacji, odczyt informacji o pakiecie, odbiór z limitem czasu
oraz odbiór ciągły.

Wariant `PROBE` nie nadaje. Sprawdza dostępne funkcje radia, kalibrację,
bieżące RSSI, wykrywanie aktywności w kanale (CAD) i tryb czuwania.

Warianty `LINK` i `LINK_RESPONDER` zamiast prostego ping/pong przesyłają
polecenia przez `hal_lora_commands` i `hal_lora_link`. Inicjator wysyła
500-bajtowe binarne żądanie `echo` na adres `0x1002`. Drugie urządzenie
wykonuje polecenie przez wspólny router i zwraca te same bajty w odpowiedzi
przypisanej do żądania.

Żądanie i odpowiedź zajmują po trzy niezaszyfrowane fragmenty. Wymiana pozwala
sprawdzić format komunikatów, identyfikatory żądań, wybór procedury obsługi,
dopasowanie odpowiedzi, dzielenie i składanie danych, odrzucanie duplikatów
oraz ponawianie transmisji z limitem prób.

Reguła dla `echo` dopuszcza źródła `LORA_LINK` i `BLE_STREAM`. Ten projekt
uruchamia tylko komunikację LoRa. Wariant poleceń z przykładu 26 używa tej
samej reguły dla uwierzytelnionego BLE Stream.

Jeżeli profil płytki udostępnia diodę stanu GPIO, świeci ona podczas nadawania
i zapala się na 120 ms po odebraniu pakietu. Brak takiej diody nie zmienia
pracy radia.

Przykład buduje się dla zwykłych płytek `pico` i `nucleo-g474re`
i zakłada zewnętrzny moduł Waveshare Core1262-HF podłączony jak w tabeli
poniżej. Dla zintegrowanej płytki LF wybierz `rp2040-lora-lf`; jej profil
opisuje już radio. Jej częstotliwość 434,0 MHz jest ustawieniem testowym,
a nie deklaracją zgodności z przepisami w dowolnym regionie.
Nie łącz w jednej parze radiowej urządzeń LF i HF.

## Kompilacja

Uruchom z głównego katalogu repozytorium:

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 27_lora_point_to_point
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 27_lora_point_to_point
```

Dispatcher kompiluje inicjator i wszystkie warianty zadeklarowane
w `hal_project_config.h`: `PROBE`, `RESPONDER`, parę do testów sprzętowych
`SF7` i `RESPONDER_SF7` oraz `LINK` i `LINK_RESPONDER`.

Aby zbudować tylko parę wymieniającą polecenia, wybierz warianty w VS Code
lub uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --variant LINK
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --variant LINK_RESPONDER
```

Dla dwóch zintegrowanych płytek Waveshare LF wybierz
`--target rp2040 --board rp2040-lora-lf` oraz warianty `LINK` i
`LINK_RESPONDER`. Procedurę wgrywania, stały wybór portu szeregowego i kryteria
`JHCMD1` opisują
[testy sprzętowe poleceń przesyłanych przez LoRa](../../tests/hardware/lora_sx1262/README.pl.md).

**Polecenia w tym przykładzie nie są szyfrowane.** Dane mają sumę CRC,
która nie zastępuje uwierzytelnienia. Włączenie szyfrowanego łącza wymaga
również `HAL_ENABLE_CRYPTO`, przygotowanego 32-bajtowego sekretu i
identyfikatora sesji, którego nie wolno ponownie użyć dla tego samego adresu
i klucza. Przed włączeniem AEAD przeczytaj
[opis API `hal_lora_link`](../../doc/api/pl/22_lora_link.md).

## Połączenia zewnętrznego Core1262-HF

Piny są zapisane w `lora_example_radio.h`; zmień je tam, jeśli moduł jest
podłączony inaczej. Parametry elektryczne samego modułu daje
`hal_lora_sx126x_core1262_hf_defaults()`.

| Sygnał | Rodzina RP | STM32G474 |
|---|---|---|
| MISO / MOSI / SCK | GP16 / GP19 / GP18 | PB14 / PB15 / PB13 |
| CS | GP17 | PB0 |
| RESET / BUSY / DIO1 | GP20 / GP21 / GP22 | PB1 / PB2 / PB3 |
| RXEN / TXEN | GP10 / GP11 | PB4 / PB5 |

Zasil moduł napięciem 3,3 V i używaj takich samych poziomów logicznych.
Połącz masy, zastosuj lokalne kondensatory odsprzęgające i podłącz właściwą
antenę HF przed rozpoczęciem nadawania. Sterownik używa SPI 8 MHz,
czeka na zwolnienie BUSY, steruje TCXO przez DIO3 i osobno obsługuje
RXEN oraz TXEN. Waveshare nazywa te linie od toru, który wyłączają: RXEN ma
stan wysoki podczas nadawania, a TXEN podczas odbioru.

Na NUCLEO-G474RE użyto SPI2, bo PA5, zegar SPI1 na złączu Arduino, steruje
diodą LD2, której przykład używa jako `HAL_LED_BUILTIN`.

Dostępne połączenia tworzą dwa oddzielne zestawy testowe: dwie zintegrowane
płytki LF albo dwa zewnętrzne moduły HF podłączone do RP2040 i STM32G474.
