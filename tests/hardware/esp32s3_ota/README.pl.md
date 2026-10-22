# Sprzętowy test OTA ESP32-S3

`tests/hardware/esp32s3_ota` weryfikuje OTA surowej aplikacji ESP-IDF
w profilu `waveshare-esp32-s3-zero`: odkrywanie, AUTH2, odrzucanie błędnych
uploadów, uruchomienie próbne, odrzucony upload w trakcie próby, wycofanie
niepotwierdzonego obrazu, potwierdzenie i powrót do pierwszego obrazu. Płytka
potrzebuje natywnego kabla USB i sieci WiFi. Komputer potrzebuje Pythona
z `pyserial` i musi przyjmować połączenie zwrotne OTA na TCP/8266 oraz
odpowiedzi odkrywania na UDP/8266 (zob. sekcję o zaporze w
[OTAWorkflow.md](../../../doc/pl/OTAWorkflow.md)).

Firmware łączy się z siecią jako `jh-ota-esp32s3`, uruchamia usługę OTA na
UDP/8266 i odpowiada na jednobajtowe polecenia na porcie szeregowym: `S`
wypisuje linię stanu `JH_ESP32_OTA` (obraz, adres, tryb startu, wersje, błędy
OTA), `C` potwierdza działający obraz, a `R` restartuje płytkę. Wariant
`IMAGE_B` różni się tylko nazwą zgłaszanego obrazu.

Weryfikator sprawdza:

- obraz A działa stabilnie i odpowiada na odkrywanie unicastem z rozmiarem
  slotu;
- błędne hasło, odpowiedź AUTH2 powtórzona dla nowego nonce, odpowiedź
  z innego źródła UDP i odpowiedź w formacie sprzed AUTH2 są odrzucane albo
  ignorowane;
- obraz systemu plików (komenda 100) jest odrzucany;
- upload, który komputer kończy po uwierzytelnieniu, jest porzucany od razu,
  a nie po timeoucie odbioru;
- obraz z MD5 innym niż w zaproszeniu, dane niebędące aplikacją ESP,
  zaproszenie większe niż slot i transfer przerwany w połowie są odrzucane,
  a obraz A działa dalej;
- obraz B zostaje przyjęty i startuje na próbę; upload w trakcie próby jest
  odrzucany; restart bez potwierdzenia wraca do A;
- obraz B ponownie, potwierdzony, zostaje po restarcie;
- obraz A wgrany i potwierdzony: płytka kończy w stanie początkowym.

## Ustawienia

Utwórz `ota_test_config.h` obok `app.cpp`. Git ignoruje ten plik:

```c
#pragma once
#define JH_OTA_TEST_SSID "nazwa-sieci"
#define JH_OTA_TEST_WIFI_PASSWORD "haslo-sieci"
#define JH_OTA_TEST_PASSWORD "haslo-ota"
```

Weryfikator czyta hasło OTA z tego pliku.

## Build, wgranie i weryfikacja

Każdy obraz buduje się do własnego katalogu, więc oba pozostają dostępne:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_ota --assembly esp32s3-zero \
  --name jh_esp32_ota --output .build/esp-idf/image-a
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_ota --assembly esp32s3-zero \
  --name jh_esp32_ota --variant IMAGE_B --output .build/esp-idf/image-b

python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_ota --assembly esp32s3-zero \
  --name jh_esp32_ota --output .build/esp-idf/image-a \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_ota/verify_ota.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

`--output` jest względny wobec projektu. Wgranie przez port szeregowy zapisuje
tablicę partycji, dane OTA i obraz A, więc płytka startuje ze slotu 0.
Weryfikator wypisuje jedną linię JSON na sprawdzenie i podsumowanie; kończy
się błędem, gdy którekolwiek sprawdzenie zawiedzie.

## Pokrycie sprzętowe

Test przechodzi na Waveshare ESP32-S3-Zero. Nie obejmuje zaniku zasilania
w trakcie transferu ani tuż po przełączeniu partycji startowej; to wymaga
sterowanego wyłącznika zasilania.
