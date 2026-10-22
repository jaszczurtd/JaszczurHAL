# Sprzętowy test sieci ESP32-S3

`tests/hardware/esp32s3_network` uruchamia natywny backend sieci ESP-IDF
w profilu `waveshare-esp32-s3-zero` z fixture'em na komputerze w tej samej
sieci LAN. Płytka potrzebuje tylko natywnego kabla USB i sieci WiFi. Komputer
potrzebuje Pythona z `pyserial` i musi przyjmować połączenia przychodzące na
portach fixture'u (18190-18193).

Firmware sprawdza:

- tryb stacji, skanowanie, które znajduje skonfigurowaną sieć, połączenie
  z błędnym hasłem (`HAL_EAUTH`) i z nieistniejącą siecią (`HAL_ENOENT`);
- połączenie z DHCP, rozwiązanie nazwy `example.com` i ping do komputera;
- echo UDP z portem nadawcy oraz odbiór, który czeka do końca timeoutu;
- echo TCP, odrzucone połączenie, koniec strumienia po zamknięciu przez drugą
  stronę i backpressure: wysyłanie do komputera, który nie czyta, wraca od razu
  z częściową liczbą bajtów, aż gniazdo zgłosi `HAL_EAGAIN`;
- unieważnienie otwartego gniazda TCP przez rozłączenie, ponowne połączenie
  i echo TCP po nim;
- wyłączenie WiFi przy otwartym gnieździe UDP, ponowne włączenie, nowe
  połączenie oraz DNS i UDP po nim;
- serwery HTTP i WebSocket uruchomione przez `hal_http_server_start_ex()`
  i `hal_websocket_server_start_ex()` na adresie płytki; komputer pobiera
  stronę i wymienia wiadomość WebSocket.

Dioda stanu miga na niebiesko, gdy firmware czeka na komputer, świeci na
niebiesko w trakcie testów, a na koniec zmienia kolor na zielony (PASS) albo
czerwony (FAIL).

## Ustawienia sieci

Utwórz `net_test_config.h` obok `app.cpp`. Git ignoruje ten plik, więc dane
sieci zostają na komputerze:

```c
#pragma once
#define JH_NET_TEST_SSID "nazwa-sieci"
#define JH_NET_TEST_PASSWORD "haslo-sieci"
#define JH_NET_TEST_HOST "192.168.1.10"  /* komputer z verify_network.py */
```

Firmware ogłasza adres komputera i swoje porty, a weryfikator obsługuje
dokładnie je. Numery portów istnieją tylko w `app.cpp`.

## Build, wgranie i weryfikacja

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_network \
  --assembly esp32s3-zero \
  --name jh_esp32_network_hardware

python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_network \
  --assembly esp32s3-zero \
  --name jh_esp32_network_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_network/verify_network.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

Weryfikator uruchamia swoje usługi po linii `ready`, wysyła `GO`, sprawdza
serwery, gdy firmware zgłosi `check=serve`, a potem wysyła `DONE`. Kończy się
sukcesem tylko wtedy, gdy każdy test przeszedł, żadnego nie brakuje i oba
sprawdzenia z komputera się udały; na końcu wypisuje podsumowanie JSON.

## Pokrycie sprzętowe

Test przechodzi na Waveshare ESP32-S3-Zero. Poza nim testów sprzętowych
nadal wymagają: IPv6, utrata adresu z DHCP, równoczesne użycie gniazd z obu
tasków, zamykanie w trakcie operacji blokujących oraz ścieżki TLS, MQTT, OTA
i WireGuard.
