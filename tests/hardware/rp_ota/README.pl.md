# Sprzętowy test natywnego OTA na RP

`tests/hardware/rp_ota` weryfikuje odkrywanie i uwierzytelnianie OTA,
potwierdzany transfer fragment po fragmencie, próbny rozruch (trial boot),
jawne potwierdzenie, drugą niepotwierdzoną próbę, w trakcie której urządzenie
odrzuca nowy obraz, automatyczne wycofanie (rollback) oraz odzyskiwanie
sieci/USB po każdym restarcie. Ten sam fixture
zwiększa licznik rozruchów w dwubankowym `hal_kv` i montuje osobną partycję
LittleFS, potwierdzając zachowanie obu użytkowników trwałego storage podczas
zmian obszarów programu, stagingu i sterowania OTA. Obsługuje Pico W/RP2040
oraz Pico 2 W/RP2350 ARM w konfiguracjach bare-metal i FreeRTOS, a także zwykłego
Pico/RP2040 podłączonego do bezprzewodowego modułu PIM730/RM2.

Skopiuj lokalny szablon sekretu i zastąp wszystkie wartości. Wynikowy
nagłówek jest ignorowany przez Git:

```sh
cp tests/hardware/rp_ota/ota_test_secrets.example.h \
  tests/hardware/rp_ota/ota_test_secrets.h
```

Weryfikator odczytuje hasło OTA z tego ignorowanego nagłówka. Zmienna
środowiskowa może je nadpisać w razie potrzeby:

```sh
export JH_OTA_TEST_PASSWORD='the-value-from-ota_test_secrets.h'
```

`device_tree.json` opisuje sprzęt każdego montażu: `picow` (Pico W,
domyślny), `pico2w` (Pico 2 W) i `pico-rm2`, czyli zwykłe Pico z zewnętrznym
modułem PIM730/RM2 podłączonym według tabeli poniżej. `pico-rm2` to nazwa
montażu w tym projekcie, a nie profil płytki; użyj `--assembly pico-rm2`.
Płytką bazową nadal jest `pico`.

Skompiluj, wgraj i zweryfikuj wariant bare-metal dla Pico W:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota --assembly picow
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota --assembly picow \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --assembly picow --runtime baremetal
```

Dla Pico 2 W użyj `--assembly pico2w`. Dodaj `--variant FREERTOS` zarówno do
kompilacji, jak i wgrywania, a następnie przekaż `--runtime freertos` do
weryfikatora dla wariantu FreeRTOS. Stanowisko
przydziela zadaniu aplikacji stos 8 KiB w konfiguracjach FreeRTOS, ponieważ
inicjalizacja CYW43 i obsługa OTA przekraczają ogólny domyślny rozmiar 2 KiB.

Dla Pico+PIM730 podłącz moduł do zwykłego Pico w następujący sposób:

| PIM730 | Sygnał Pico | Fizyczny pin |
|---|---|---|
| `WL_ON` | GP2 | 4 |
| `CS` | GP3 | 5 |
| `DAT` | GP4 | 6 |
| `CLK` | GP5 | 7 |
| `GND` | GND | 8 |
| `3V3` | 3V3(OUT) | 36 |

`DAT` to połączony dwukierunkowy sygnał danych/wybudzania hosta. Pozostaw
`BL_ON`, `GPIO0..2` oraz `N/C` niepodłączone. Skompiluj i zweryfikuj ten
montaż jawnie:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_ota --assembly pico-rm2
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_ota --assembly pico-rm2 \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_ota/verify_ota.py \
  --port /dev/serial/by-id/<device> \
  --assembly pico-rm2 --runtime baremetal
```

Dodaj `--status-only`, aby zweryfikować tożsamość stanowiska, gotowość sieci
oraz automatyczną telemetrię zegara gSPI bez generowania ani przesyłania
obrazów OTA. Ten tryb diagnostyczny nie wymaga hasła OTA ani artefaktów
kompilacji.

Dla przebiegu FreeRTOS użyj `--variant FREERTOS` i `--runtime freertos`.

Każdy montaż i wariant kompiluje się do własnego katalogu,
`.build/hardware/rp_ota/<target>/<assembly>`, a FreeRTOS do
`variants/FREERTOS` pod nim. Weryfikator czyta stamtąd artefakty wybranego
montażu i runtime; `--artifact-dir` wskazuje mu inne wyjście kompilacji.

Pico W i Pico 2 W mogą pozostać podłączone razem. Ich nazwy hostów OTA to
`jh-ota-rp2040` i `jh-ota-rp2350-arm`, więc odkrywanie nigdy nie zgaduje
między nimi. Pico W i Pico+PIM730 współdzielą nazwę hosta RP2040; gdy oba są
zasilone, przekaż adres IP wybranego urządzenia do `--broadcast`. Odpowiedź
statusu CDC zawiera i weryfikator sprawdza nazwę montażu, aktualny adres
IPv4, target, runtime i stan rozruchu OTA, plus aktywne `clk_sys`, żądaną i
efektywną szybkość gSPI, dzielnik 16,8, wybrany program timingu PIO, licznik
rozruchów w KV oraz stan montowania/formatowania LittleFS.
Weryfikator tworzy podpisane kontenery A/B w katalogu artefaktów
i pozostawia urządzenie w stabilnym obrazie A po udowodnieniu wycofania z
obrazu B.

`hal_ota_begin()` publikuje też skonfigurowaną nazwę hosta przez mDNS.
Podczas gdy sonda jest podłączona, zweryfikuj responder niezależnie, na
przykład przez `getent hosts jh-ota-rp2040.local` (lub nazwę hosta RP2350).
Nazwa hosta WiFi jest wysyłana w opcji DHCP 12, a jej zmiana przy aktywnej
dzierżawie wyzwala jej odnowienie.

Callback wgrywania OTA to połączenie TCP od płytki do hosta na TCP/8266, a
odpowiedzi na wyszukiwanie przez broadcast wracają do hosta na UDP/8266.
Uruchom `./runmefirst.sh` i zatwierdź jego trwałe reguły OTA ograniczone do
sieci LAN przed testem sprzętowym. Zweryfikuj reguły bez ich zmiany za pomocą:

```sh
python3 scripts/configure_ota_firewall.py --check
```

Na natywnym Windowsie najpierw zweryfikuj, że wybrana zaufana sieć LAN ma
profil połączenia `Private`, a następnie sprawdź dokładny plan reguły bez
zmieniania hosta:

```powershell
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --dry-run `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
```

Zastosuj ten sam zakres z już podniesionego (elevated) PowerShell po usunięciu
`--dry-run`. Skrypt prosi o potwierdzenie, tworzy idempotentną regułę
Windows Defender Firewall ograniczoną do profilu `Private`, interfejsu,
podsieci źródłowej i TCP/8266, i nigdy nie zmienia samego profilu sieci.
Weryfikacja sprzętowa na Windows używa zarządzanego pliku wykonywalnego
Pythona i akceptuje port COM, na przykład `--port COM3`.

Jeśli sieć testowa różni się od sieci wybranej podczas wstępnej konfiguracji,
uruchom ponownie skrypt z jawnymi `--interface` i `--network`. Jeśli
odkrywanie przez broadcast ograniczony (limited-broadcast) jest zablokowane,
ale adres IP urządzenia jest znany, skieruj wykrywanie bezpośrednio pod ten
adres:

```sh
python3 tests/hardware/rp_ota/verify_ota.py \
  <other-options> --broadcast <device-ip>
```

Gdy fabryczny UF2 zastępuje aktywny program obrazem dla innej kombinacji
target/runtime lub płytka ponownie użyta przez inne stanowisko flash zgłasza
nieprawidłowy stan początkowy OTA, wejdź w BOOTSEL i wykasuj tylko cztery
sektory kontrolne OTA przed wgraniem nowego UF2. Stare metadane zawierają
skrót (digest) poprzedniego aktywnego programu i nie mogą być sparowane z
zastępczym obrazem. Bezwzględne zakresy to `0x101fc000..0x10200000` dla
Pico W lub Pico+PIM730 oraz `0x103fc000..0x10400000` dla Pico 2 W:

```sh
.build/tools/picotool/picotool erase -r <range-start> <range-end> \
  --bus <usb-bus> --address <usb-address>
```

Ta zautomatyzowana sonda nie symuluje utraty zasilania podczas trwającej
podmiany obrazu flash. Walidacja utraty zasilania wymaga sterowanego
przełącznika zasilania i jest osobnym, destrukcyjnym przebiegiem
odzyskiwania.
