# 27 - Łącze LoRa punkt-punkt

Niskopoziomowy przykład ping/pong SX1262 obejmuje asynchroniczne nadawanie i
odbiór sterowane przez DIO1, funkcje zwrotne, anulowanie operacji, metadane
pakietów, ograniczone czasy oczekiwania oraz odbiór ciągły. Jedno urządzenie
zbuduj jako inicjator, a drugie z wariantem `responder`. Wariant `probe`, który
nie nadaje, sprawdza obsługiwane funkcje, jawną kalibrację, bieżące RSSI, CAD i
tryb czuwania.

W wariantach `link` i `link-responder` niskopoziomową aplikację ping/pong
zastępuje `hal_lora_commands` działające na `hal_lora_link`. Inicjator wysyła
binarne żądanie `echo` o rozmiarze 500 bajtów na adres `0x1002`. Responder
przekazuje je do wspólnego routera poleceń i zwraca identyczne bajty w
odpowiedzi powiązanej z tym samym żądaniem. Żądanie i odpowiedź zajmują po trzy
niezaszyfrowane fragmenty łącza, dlatego udana transakcja sprawdza ramkowanie
poleceń, identyfikatory żądań, wybór właściwej procedury obsługi, powiązanie
odpowiedzi z żądaniem, fragmentację, składanie, tłumienie duplikatów i
ograniczone ponawianie transmisji.

Niezależna od transportu trasa `echo` dopuszcza źródła `LORA_LINK` i
`BLE_STREAM`. Ten przykład dołącza adapter LoRa; wariant poleceń BLE przykładu
26 ponownie wykorzystuje tę samą politykę trasy przez uwierzytelniony BLE
Stream.

Gdy wybrana płytka udostępnia diodę stanu sterowaną przez GPIO, świeci ona
podczas nadawania i pulsuje przez 120 ms po odebraniu pakietu. Płytki bez takiej
diody zachowują identyczne działanie radia bez sygnalizacji wizualnej.

Domyślne konfiguracje RP2040 i STM32G474 wybierają stałe profile testowe
`pico-core1262-hf` i `nucleo-g474re-core1262-hf` z modułami Waveshare
Core1262-HF. Dla zintegrowanej płytki LF jawnie wybierz profil
`rp2040-lora-lf`. Ta konfiguracja testowa świadomie ustawia częstotliwość
434,0 MHz i nie stanowi uniwersalnego ustawienia zgodnego z przepisami dla
każdego regionu. Nie próbuj łączyć urządzeń LF i HF przez radio.

## Kompilacja

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 27_lora_point_to_point
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 27_lora_point_to_point
```

Reprezentatywna bramka kompiluje bazowego inicjatora oraz `probe`, `responder`,
`link` i `link-responder`. Warianty sprzętowe `sf7` i `responder-sf7` pozostają
dostępne przez `jh-vscode`, ale nie należą do reprezentatywnej kompilacji bramki.

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

Dla dwóch zintegrowanych płytek Waveshare LF użyj
`--target rp2040 --board rp2040-lora-lf` z wariantami `link` i
`link-responder`. Pełna procedura
wgrywania, stabilny wybór portu szeregowego i kryteria akceptacji `JHCMD1`
znajdują się w [głównej sprzętowej bramce routera poleceń](../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-routera-poleceń-sx1262-przez-lora).

Przykład poleceń celowo używa niezaszyfrowanych danych zabezpieczonych sumą CRC.
Szyfrowane łącza wymagają także `HAL_ENABLE_CRYPTO`, wcześniej wprowadzonego
32-bajtowego sekretu i identyfikatora sesji, którego nigdy nie wolno ponownie
użyć dla tej samej pary adresu i klucza. Przed włączeniem AEAD przeczytaj
[API niezawodnego łącza LoRa](../../doc/api/pl/22_lora_link.md).

## Połączenia zewnętrznego Core1262-HF

| Sygnał | Rodzina RP | STM32G474 |
|---|---|---|
| MISO / MOSI / SCK | GP16 / GP19 / GP18 | PB14 / PB15 / PB13 |
| CS | GP17 | PB0 |
| RESET / BUSY / DIO1 | GP20 / GP21 / GP22 | PB1 / PB2 / PB3 |
| RXEN / TXEN | GP10 / GP11 | PB4 / PB5 |

Zasil moduł i wszystkie I/O napięciem 3,3 V, połącz masy, dodaj lokalne
odsprzęganie i podłącz właściwą antenę HF przed nadawaniem. Sterownik używa SPI
8 MHz, czeka na BUSY, steruje TCXO przez DIO3 i niezależnie kontroluje
RXEN/TXEN. Na NUCLEO-G474RE SPI2 celowo omija PA5, aby fizycznie podłączony LD2
i publiczny `HAL_LED_BUILTIN` pozostały dostępne. Nie ukrywaj urządzenia na
płytce bazowej w profilu złożonym tylko po to, aby ponownie użyć nadal
podłączonego pinu.

Dostępny sprzęt pozwala zestawić dwa osobne testy: dwie zintegrowane płytki LF
albo dwa zewnętrzne moduły HF podłączone do hostów RP2040 i STM32G474.
