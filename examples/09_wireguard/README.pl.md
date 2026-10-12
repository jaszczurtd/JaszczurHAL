# 09 - Połączenie z siecią przez WireGuard

Przykład łączy urządzenie z WiFi, konfiguruje tunel WireGuard i co pięć sekund
sprawdza połączenie z drugim końcem tunelu. Jeśli nie jest ono aktywne,
wywołuje `hal_wireguard_kick_handshake_text()` z adresem testowym, aby
zainicjować uzgadnianie połączenia.

## Konfiguracja

W `app.c` ustaw dane WiFi oraz parametry tunelu: lokalny adres IP, klucz
prywatny urządzenia, adres i port drugiej strony, jej klucz publiczny oraz
sieć dostępną przez tunel. Odpowiadają im stałe `WIFI_*` i `WG_*`.
`WG_PROBE_IP` i `WG_PROBE_PORT` określają adres oraz port używane do
zainicjowania ruchu testowego.

Napisy `base64-private-key`, `base64-peer-public-key` i `vpn.example.com`
są miejscami na własne dane, nie działającą konfiguracją. Skonfiguruj także
drugą stronę tunelu. Obsługę WireGuard włącza `HAL_ENABLE_WIREGUARD`.

Komunikat `tunnel started` oznacza pomyślne wywołanie inicjalizacji tunelu,
nie potwierdzenie połączenia z drugą stroną. Jego stan jest sprawdzany
oddzielnie przez `hal_wireguard_peer_up()`.

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/09_wireguard --assembly picow
```

`device_tree.json` opisuje sprzęt każdego montażu: `picow` (Pico W,
domyślny), `pico2w` (Pico 2 W), `esp32s3-zero` (ESP32-S3-Zero z własnym
WiFi) i `nucleo-pim730`. `nucleo-pim730` to zwykła NUCLEO-G474RE
z zewnętrznym modułem PIM730/RM2, bez osobnego profilu płytki. Tabela poniżej
powtarza połączenia modułu. Projekt nie obejmuje RP2350 RISC-V.

| Sygnał PIM730/RM2 | NUCLEO-G474RE |
|---|---|
| CS | PB12, pin 16 CN10 |
| DAT | PB15, pin 26 CN10 |
| WL_ON | PB14, pin 28 CN10 |
| CLK | PB13, pin 30 CN10 |
