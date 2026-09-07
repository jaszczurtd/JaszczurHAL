<a id="27---łącze-lora-punkt-punkt"></a>

# 27 - Wymiana pakietów i poleceń przez LoRa

Przykład pozwala połączyć dwa urządzenia z radiem SX1262. W wersji podstawowej
jedno wysyła pakiet, a drugie odpowiada - zbuduj je odpowiednio jako
inicjator i wariant `responder`. Obsługa nadawania i odbioru jest
asynchroniczna, wykorzystuje DIO1 i funkcje zwrotne. Kod pokazuje też
anulowanie operacji, odczyt informacji o pakiecie, odbiór z limitem czasu
oraz odbiór ciągły.

Wariant `probe` nie nadaje. Sprawdza dostępne funkcje radia, kalibrację,
bieżące RSSI, wykrywanie aktywności w kanale (CAD) i tryb czuwania.

Warianty `link` i `link-responder` zamiast prostego ping/pong przesyłają
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

Domyślne profile to `pico-core1262-hf` i `nucleo-g474re-core1262-hf`
z modułami Waveshare Core1262-HF. Dla zintegrowanej płytki LF wybierz
`rp2040-lora-lf`. Jej częstotliwość 434,0 MHz jest ustawieniem testowym,
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

Podstawowy zestaw kontroli kompilacji obejmuje inicjator oraz `probe`,
`responder`, `link` i `link-responder`. Warianty sprzętowe `sf7` oraz
`responder-sf7` można zbudować przez `jh-vscode`, ale nie należą do tego
zestawu.

Aby zbudować tylko parę wymieniającą polecenia, wybierz warianty w VS Code
lub uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico-core1262-hf --variant link
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re-core1262-hf \
  --variant link-responder
```

Dla dwóch zintegrowanych płytek Waveshare LF wybierz
`--target rp2040 --board rp2040-lora-lf` oraz warianty `link` i
`link-responder`. Procedurę wgrywania, stały wybór portu szeregowego i kryteria
`JHCMD1` opisują
[testy sprzętowe poleceń przesyłanych przez LoRa](../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-routera-poleceń-sx1262-przez-lora).

**Polecenia w tym przykładzie nie są szyfrowane.** Dane mają sumę CRC,
która nie zastępuje uwierzytelnienia. Włączenie szyfrowanego łącza wymaga
również `HAL_ENABLE_CRYPTO`, przygotowanego 32-bajtowego sekretu i
identyfikatora sesji, którego nie wolno ponownie użyć dla tego samego adresu
i klucza. Przed włączeniem AEAD przeczytaj
[opis API `hal_lora_link`](../../doc/api/pl/22_lora_link.md).

## Połączenia zewnętrznego Core1262-HF

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
RXEN oraz TXEN.

Na NUCLEO-G474RE użyto SPI2, aby nie zajmować PA5. Pin pozostaje połączony
z diodą LD2 i jest dostępny jako `HAL_LED_BUILTIN`. Profil płytki z modułem
nie powinien ukrywać tej diody ani przedstawiać jej pinu jako niepodłączonego.

Dostępne połączenia tworzą dwa oddzielne zestawy testowe: dwie zintegrowane
płytki LF albo dwa zewnętrzne moduły HF podłączone do RP2040 i STM32G474.
