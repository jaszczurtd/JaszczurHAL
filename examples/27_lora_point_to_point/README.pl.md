# 27 - LoRa point-to-point

Surowy przykład ping/pong SX1262 obejmuje asynchroniczne nadawanie i odbiór
sterowane DIO1, callbacki, anulowanie, metadane pakietu, ograniczone timeouty
odbioru i odbiór ciągły. Jedno urządzenie zbuduj jako inicjator, a drugie z
wariantem `responder`. Wariant `probe`, który nie nadaje, sprawdza capabilities,
jawną kalibrację, bieżące RSSI, CAD i standby.

Warianty `link` i `link-responder` zastępują surową aplikację ping/pong przez
`hal_lora_commands` działające na `hal_lora_link`. Inicjator wysyła binarne
żądanie `echo` o rozmiarze 500 bajtów na adres `0x1002`. Responder przekazuje je
do wspólnego routera poleceń i zwraca identyczne bajty w skorelowanej odpowiedzi.
Żądanie i odpowiedź zajmują po trzy jawne fragmenty linku, dlatego udana
transakcja obejmuje ramkowanie poleceń, identyfikatory żądań, dispatch trasy,
korelację odpowiedzi, fragmentację, składanie, tłumienie duplikatów i
ograniczone ponawianie.

Niezależna od transportu trasa `echo` dopuszcza źródła `LORA_LINK` i
`BLE_STREAM`. Ten przykład dołącza adapter LoRa; wariant poleceń BLE przykładu
26 ponownie wykorzystuje tę samą politykę trasy przez uwierzytelniony BLE
Stream.

Gdy wybrany board udostępnia LED statusu GPIO, LED świeci podczas nadawania i
pulsuje przez 120 ms po odebraniu pakietu. Boardy bez takiego LED-a zachowują
identyczne działanie radia bez sygnalizacji wizualnej.

Domyślne konfiguracje RP2040 i STM32G474 wybierają stałe fixture
`pico-core1262-hf` i `nucleo-g474re-core1262-hf` z modułami Waveshare
Core1262-HF. Dla zintegrowanego boardu LF jawnie wybierz `rp2040-lora-lf`; używa
on celowej konfiguracji testowej 434,0 MHz, a nie uniwersalnego presetu
regulacyjnego. Nie próbuj łączyć urządzeń LF i HF przez radio.

## Build

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 27_lora_point_to_point
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 27_lora_point_to_point
```

Reprezentatywny gate buduje bazowego inicjatora oraz `probe`, `responder`,
`link` i `link-responder`. Warianty sprzętowe `sf7` i `responder-sf7` pozostają
dostępne przez `jh-vscode`, ale są wyłączone z reprezentatywnego gate builda.

Zbuduj tylko parę poleceń przez selektor wariantu VS Code albo bezpośrednio:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico-core1262-hf --variant link
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re-core1262-hf \
  --variant link-responder
```

Dla dwóch zintegrowanych boardów Waveshare LF użyj `--target rp2040 --board
rp2040-lora-lf` z wariantami `link` i `link-responder`. Pełna procedura
wgrywania, stabilny wybór portu serial i kryteria akceptacji `JHCMD1` znajdują
się w [głównym sprzętowym gate routera poleceń](../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-routera-poleceń-sx1262-przez-lora).

Przykład poleceń celowo używa plaintext chronionego CRC. Szyfrowane linki
wymagają także `HAL_ENABLE_CRYPTO`, zapisanego 32-bajtowego sekretu i
identyfikatora sesji, który nigdy nie jest ponownie używany dla tego samego
adresu i klucza. Przed włączeniem AEAD przeczytaj
[API niezawodnego linku LoRa](../../doc/api/pl/22_lora_link.md).

## Połączenia zewnętrznego Core1262-HF

| Sygnał | Rodzina RP | STM32G474 |
|---|---|---|
| MISO / MOSI / SCK | GP16 / GP19 / GP18 | PB14 / PB15 / PB13 |
| CS | GP17 | PB0 |
| RESET / BUSY / DIO1 | GP20 / GP21 / GP22 | PB1 / PB2 / PB3 |
| RXEN / TXEN | GP10 / GP11 | PB4 / PB5 |

Zasil moduł i wszystkie I/O napięciem 3,3 V, połącz masy, dodaj lokalne
odsprzęganie i podłącz właściwą antenę HF przed nadawaniem. Driver używa SPI
8 MHz, czeka na BUSY, steruje TCXO przez DIO3 i niezależnie kontroluje
RXEN/TXEN. Na NUCLEO-G474RE SPI2 celowo omija PA5, aby fizycznie podłączony LD2
i publiczny `HAL_LED_BUILTIN` pozostały dostępne. Nie ukrywaj urządzenia boardu
bazowego w profilu złożonym tylko po to, aby ponownie użyć nadal podłączonego
pinu.

Dostępny sprzęt tworzy dwa osobne testy: dwa zintegrowane boardy LF albo dwa
zewnętrzne moduły HF podłączone do hostów RP2040 i STM32G474.
