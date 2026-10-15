# Test sprzętowy JH BLE Stream v1

`tests/hardware/bluetooth_stream` sprawdza publiczny cykl życia BLE oraz
uwierzytelniony strumień aplikacji na Raspberry Pi Pico W, Pico 2 W, RP2040
Pico z RM2/PIM730 oraz STM32G474 Nucleo z PIM730/RM2. Firmware reklamuje się
przez BLE jako `JH Stream HW`, wymaga stałego, testowego sekretu o długości
256 bitów i odsyła uwierzytelnione ładunki. Skrypt `verify.py` pełni na
Linuksie rolę urządzenia centralnego za pośrednictwem BlueZ.

## Warianty kompilacji

Skompiluj i wgraj osobno każdą z ośmiu kombinacji montażu i środowiska
wykonawczego:

| Montaż | Target | Sprzęt | Runtime |
|---|---|---|---|
| `picow` | `rp2040` | Pico W | bare-metal, FreeRTOS |
| `pico-rm2` | `rp2040` | Pico + PIM730/RM2 | bare-metal, FreeRTOS |
| `pico2w` | `rp2350-arm` | Pico 2 W | bare-metal, FreeRTOS |
| `nucleo-pim730` | `stm32g474` | NUCLEO-G474RE + PIM730/RM2 | bare-metal, FreeRTOS |

Ta tabela jest macierzą sprzętową stanowiska. `device_tree.json` opisuje
sprzęt każdego montażu, a domyślnym jest `nucleo-pim730`;
`hal_project_config.h` deklaruje wariant `FREERTOS`, a
jawnie uruchamiany `tests/test_hardware_fixture_files.py` sprawdza obecność
wszystkich ośmiu par montaż/tryb działania na liście; nie kompiluje firmware. `pico-rm2` i `nucleo-pim730` to zwykłe Pico
i zwykła NUCLEO-G474RE z zewnętrznym modułem PIM730/RM2; tabela poniżej
powtarza połączenia modułu. Te nazwy wybierają montaże, a nie profile
płytek; używaj `--assembly` także wtedy, gdy montaż ma nazwę dawnego profilu.

| Sygnał PIM730/RM2 | Pico (`pico-rm2`) | NUCLEO-G474RE (`nucleo-pim730`) |
|---|---|---|
| CS | GPIO3 | PB12, pin 16 CN10 |
| DAT | GPIO4 | PB15, pin 26 CN10 |
| WL_ON | GPIO2 | PB14, pin 28 CN10 |
| CLK | GPIO5 | PB13, pin 30 CN10 |

Warianty bare-metal zbudujesz następująco:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly pico-rm2
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly pico2w
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream --assembly nucleo-pim730
```

Dołącz `--variant FREERTOS` do każdej komendy kompilacji i wgrywania dla
obrazu FreeRTOS. Firmware testowy inicjalizuje BLE przy pierwszym wywołaniu
`app_task0()`, już po uruchomieniu schedulera FreeRTOS. Stos zadania 0 ma
1024 słowa, ponieważ uwierzytelnione uzgadnianie połączenia i używane przez nie
kryptograficzne
zmienne tymczasowe przekraczają ogólny domyślny rozmiar stanowiska. Podczas
wgrywania podaj ten sam montaż i wariant. Udana kompilacja potwierdza zbudowanie firmware, nie jego działanie na urządzeniu
i nie liczy się jako test sprzętowy.

## Warianty obciążeniowe STM32G474 PIM730 + ILI9341

Opcjonalne warianty `DISPLAY` i `DISPLAY_FREERTOS` zachowują ten sam
protokół BLE Stream i weryfikator hosta, jednocześnie ciągle aktualizując
ILI9341 podłączony do złącza SPI Arduino NUCLEO-G474RE. Wykorzystują SPI1
równolegle z dedykowanym transportem gSPI PIM730 na PB12-PB15. Te warianty
stanowią dodatkowy dowód współistnienia/obciążenia i nie zastępują żadnego z
dwóch bazowych obrazów bramki STM32 wymienionych w macierzy kompilacji.
Wyświetlacz ma tylko montaż `nucleo-pim730`; w pozostałych
`hal_project_config.h` zatrzymuje te warianty przez `#error`.

Skompiluj osobne artefakty za pomocą:

```bash
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --assembly nucleo-pim730 --variant DISPLAY

vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_stream \
  --assembly nucleo-pim730 --variant DISPLAY_FREERTOS
```

ILI9341 jest podłączony tak samo jak w `examples/07_display_media`.
W `device_tree.json` to węzeł `display` na magistrali `spi` montażu
`nucleo-pim730`; tabela powtarza te połączenia:

| ILI9341 | STM32G474 | Morpho | Arduino |
|---|---|---|---|
| `SCK` | `PB3` | pin 31 CN10 | pin 4 CN9 (`D3`) |
| `MISO` | `PA6` | pin 13 CN10 | pin 5 CN5 (`D12`) |
| `MOSI` | `PA7` | pin 15 CN10 | pin 4 CN5 (`D11`) |
| `CS` | `PB6` | pin 17 CN10 | pin 3 CN5 (`D10`) |
| `DC` | `PC7` | pin 19 CN10 | pin 2 CN5 (`D9`) |
| `RESET` | `PA9` | pin 21 CN10 | pin 1 CN5 (`D8`) |

Zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje diodą LD2.

Ekran wyświetla adres kontrolera, stan BLE/Stream, MTU, RX/TX, liczniki
odrzuceń/przepełnień/bezpieczeństwa, restarty cyklu życia, status i czas
działania. Statyczne i niezmienione pola są przerysowywane tylko wtedy, gdy
zmienia się ich wartość; RX/TX oraz status/czas działania nadal aktualizują
się raz na sekundę. Utrzymuje to trwałe obciążenie SPI1 bez wielokrotnego
przesyłania identycznych pełnych wierszy tekstu. Błąd inicjalizacji lub
aktualizacji wyświetlacza zatrzymuje normalny przebieg testu i jest zgłaszany
jako błąd cyklu życia. LCD obsługuje wyłącznie zapis, dlatego poprawność obrazu
na panelu należy potwierdzić wzrokowo.

RP2350 RISC-V jest celowo pominięte: transport Bluetooth CYW43 nie jest
włączony dla tego targetu.

## Weryfikator sprzętowy

Uruchom weryfikator po wgraniu każdego obrazu, używając adresu wypisanego
przez stanowisko. `--assembly` i `--runtime` są wymagane i muszą opisywać
wgrany obraz. Nazwy montaży i ich targety weryfikator bierze
z `device_tree.json`:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --assembly picow \
  --runtime baremetal
```

Domyślny przebieg sprawdza:

- publiczne metadane, ATT MTU, inicjalizację, rozgłaszanie, połączenie i
  uwierzytelniony strumień, w tym dokładnie jedną instancję usługi, flagi GATT
  oraz obie odmiany zapisu DATA: write-request i write-command;
- żądanie szyfrowane pełnej deinicjalizacji Stream i BLE, po którym
  następuje inicjalizacja bez resetu MCU, ze sprawdzeniem stabilnego adresu
  i generacji;
- 50 kolejnych cykli rozłączenia, ponownego połączenia, uwierzytelnienia i
  echa;
- uwierzytelniony strumień przez co najmniej 300 sekund przy docelowej
  szybkości co najmniej 10 wiadomości na sekundę, z co najmniej 90% tej
  docelowej szybkości plus sprawdzeniami sekwencji, duplikacji i
  integralności;
- nasycenie kolejki RX 12 zaszyfrowanymi ramkami, weryfikację zachowanych i
  odrzuconych ramek, jawne rozliczenie przepełnienia oraz echo po
  nasyceniu;
- przypadki błędnego dowodu, sfałszowanego tagu, ponownego użycia komunikatu
  (replay), przeskoku licznika do przodu i stopniowego wydłużania czasu między
  próbami uwierzytelnienia.

Główne parametry obciążenia są jawne i wymuszają minima akceptacji:

```bash
python3 tests/hardware/bluetooth_stream/verify.py \
  --address XX:XX:XX:XX:XX:XX \
  --assembly picow \
  --runtime baremetal \
  --reconnects 50 \
  --stream-seconds 300 \
  --stream-rate 10 \
  --saturation-frames 12 \
  --saturation-hold 5
```

`--reconnects` musi wynosić co najmniej 50, `--stream-seconds` co najmniej 300,
a `--stream-rate` co najmniej 10. Test kończy się niepowodzeniem również wtedy,
gdy rzeczywista szybkość przesyłania uwierzytelnionych wiadomości spadnie
poniżej 90% wartości `--stream-rate`. Na potrzeby wielogodzinnego testu
stabilności zwiększ czas trwania strumienia. Dla obrazu podstawowego wybierz
runtime `baremetal`, a dla wariantu `FREERTOS` - runtime `freertos`.
Weryfikator jawnie wybiera wykrywanie LE, dlatego nieaktualny alias BlueZ nie
wpływa na wybór według adresu. Wymaga systemowych pakietów Pythona dla D-Bus i
GLib oraz pakietu `cryptography`.

## Zakres weryfikacji

Po kroku z watchdogiem weryfikator wymaga powodu resetu `4` oraz nowego,
niezerowego, losowego identyfikatora rozruchu; powód resetu sprzed testu może
być dowolny. Sam restart BLE nie może więc zostać uznany za reset MCU. Komenda
watchdoga resetuje MCU, ale nie odcina VBUS, dlatego nie zastępuje fizycznego
testu utraty zasilania.

Jeszcze nie uruchomiono na sprzęcie: RP2040 Pico z RM2/PIM730 w obu runtime,
podstawowych obrazów STM32G474 + RM2/PIM730 w obu runtime ani wariantów
`DISPLAY` dla STM32G474 z zegarem SPI na PB3. Strona hosta działa wyłącznie na
Linuksie z BlueZ; natywny Windows nie jest objęty testem.

## Polecenia stanowiska testowego i zasady identyfikacji

Sterowanie tożsamością, restartem, nasyceniem i statystykami to polecenia
wyłącznie dla stanowiska testowego, przenoszone wewnątrz wzajemnie
uwierzytelnionych i zaszyfrowanych ładunków Stream DATA. Każde polecenie to
jedna kompletna ramka DATA zapisywana żądaniem `WriteValue` BlueZ do
istniejącej charakterystyki RX
`b7ce0002-3c13-4fe2-801f-d71bdab1369b`. Jej odpowiedź to jedno zaszyfrowane
powiadomienie DATA z istniejącej charakterystyki TX
`b7ce0003-3c13-4fe2-801f-d71bdab1369b`. Polecenia nie są dzielone między
zapisy GATT. Nie dodają żadnej charakterystyki i nie zmieniają protokołu
przewodowego JH BLE Stream v1.

Weryfikator dodatkowo wysyła zwykłe uwierzytelnione echo przez BlueZ
`type=command`, aby przetestować ścieżkę `write-without-response` na RX;
polecenia sterujące stanowiska używają `type=request`, dzięki czemu ich
zakończenie żądania jest obserwowalne.

| Uwierzytelniony ładunek polecenia | Odpowiedź lub efekt stanowiska |
|---|---|
| `JHBL5/IDENTITY` | `J5I1\|<target>\|<assembly>\|<runtime>` |
| `JHBL5/RESTART` | `JHBL5/RESTARTING`, następnie pełny restart Stream i BLE |
| `JHBL5/BOOT` | `J5B1` z następującym po nim jednym bajtem powodu resetu i 64-bitowym losowym identyfikatorem rozruchu w formacie little-endian |
| `JHBL5/POWER-LOSS` (RP i STM32G474) | `JHBL5/POWER-LOSS-ARMED`, następnie reset watchdoga bez interwencji hosta lub użytkownika |
| `JHBL5/SATURATE` + czas trwania little-endian | `JHBL5/SATURATE-READY`, następnie ograniczona pauza RX |
| `JHBL5/STATS` | zwarty binarny rekord `J5S1` potwierdzający odzyskanie |

Odpowiedź tożsamości jest kompilowana bezpośrednio z `HAL_TARGET_NAME`,
`JH_HW_ASSEMBLY_NAME` i `HAL_ENABLE_FREERTOS`; runtime to dokładnie
`baremetal` lub `freertos`. Nazwa montażu zastępuje nazwę płytki, bo
`pico-rm2` i `nucleo-pim730` nie mają własnego profilu płytki. Przed
akceptacją wyników obciążenia weryfikator porównuje odpowiedź z wybranym
montażem, jego targetem i wartością `--runtime`. Na przykład odpowiedź Pico W
bare-metal to `J5I1|rp2040|picow|baremetal`, a Nucleo z PIM730 odpowiada
`J5I1|stm32g474|nucleo-pim730|baremetal`.

Udany przebieg fizyczny kończy się `JHBL5 HOST PASS`. Log urządzenia używa
prefiksu `JHBL5` i rejestruje wynegocjowane MTU, liczniki, błędy
uwierzytelniania, odrzucenia powtórek, błędy cyklu życia, restarty oraz
straty ograniczonej kolejki.

Końcowa faza bezpieczeństwa celowo uruchamia mechanizm opóźniania kolejnych
prób uwierzytelnienia. Przez całe skonfigurowane okno 30 sekund wysyła co
najmniej raz na sekundę odrzucane komunikaty HELLO. Dopiero po upływie tego
czasu potwierdza odzyskanie nowej, uwierzytelnionej sesji i wypisuje
`JHBL5 HOST PASS`. Udany
przebieg pozostawia więc stanowisko gotowe do następnego testu bez ponownego
wgrywania firmware'u.

Wbudowany sekret i jego kopia w `verify.py` to publiczny materiał testowy.
Nigdy nie mogą być ponownie użyte przez produkt. Produkt potrzebuje unikalnego,
losowego sekretu przypisanego do urządzenia, dostarczonego niezależnym kanałem
(`out of band`) i zapisanego podczas konfiguracji urządzenia (provisioningu).

## Podstawowy test routera poleceń BLE

Warianty `COMMANDS` z `examples/26_ble_stream` sprawdzają osobny adapter
`hal_ble_commands`, podczas gdy bazowy firmware używany w tym teście nadal
sprawdza bezpośrednio dane Stream. System Linux z BlueZ działa w roli Central, a każda
płytka pozostaje urządzeniem Peripheral.

Zbuduj i wgraj obrazy bare metal oraz FreeRTOS na dwie płytki Pico W. Gdy obie są
już w BOOTSEL, wybierz jawnie każdy wolumin:

```bash
vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --assembly picow --variant COMMANDS \
  --bootsel-volume /dev/<baremetal-partition>

vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --assembly picow --variant COMMANDS_FREERTOS \
  --bootsel-volume /dev/<freertos-partition>
```

Odczytaj adres BLE każdej płytki z logu USB CDC i uruchom krótki program
weryfikujący dla obu jawnie podanych adresów. `--assembly` wskazuje montaż
z `examples/26_ble_stream/device_tree.json`; weryfikator oczekuje
w zdarzeniu gotowości przykładu targetu i płytki tego montażu:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --assembly picow --runtime baremetal

python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address YY:YY:YY:YY:YY:YY \
  --assembly picow --runtime freertos
```

Zaliczenie wymaga dokładnego złożenia binarnego echo o rozmiarze 500 bajtów w
obu kierunkach, wartości `BLE_STREAM` wskazującej źródło, wszystkich czterech
flag bezpieczeństwa, niezerowych identyfikatorów drugiej strony i sesji,
`HAL_EPERM` dla trasy ograniczonej do źródła, `HAL_ENOENT` dla nieznanej trasy,
zdarzenia wysłanego przez Peripheral, wymiany żądania i odpowiedzi oraz nowej uwierzytelnionej
sesji po ponownym połączeniu. Podczas pełnej weryfikacji zestawu z dwiema
płytkami zamień obrazy między fizycznymi płytkami i powtórz test.
