# 08 - Publikowanie i odbieranie wiadomości MQTT

Przykład łączy urządzenie z WiFi i brokerem MQTT. Co pięć sekund wysyła
wiadomość JSON z licznikiem i wartością RSSI do
`jaszczurhal/example/telemetry`. Subskrybuje też `jaszczurhal/example/cmd`
i wypisuje odebrane wiadomości w konsoli.

Gdy brakuje połączenia, próby połączenia z WiFi lub brokerem są rozdzielone
co najmniej pięcioma sekundami. Po połączeniu pętla aplikacji regularnie
wywołuje `hal_mqtt_loop()`.

## Konfiguracja

W `app.c` ustaw `WIFI_SSID` i `WIFI_PASSWORD`. Sprawdź także `MQTT_HOST`,
`MQTT_PORT`, `MQTT_CLIENT_ID`, `MQTT_TOPIC_PUB` i `MQTT_TOPIC_SUB`.
Wartości domyślne wskazują `broker.hivemq.com` i port `1883`.
Ten kod nie konfiguruje TLS ani uwierzytelniania klienta MQTT, ale taka
funkcjonalność jest również wspierana.

Wypisywana wiadomość jest skracana do 95 bajtów, aby zmieściła się w buforze
diagnostycznym. Skrócenie dotyczy tylko komunikatu w konsoli, nie odbieranej
wiadomości.
Obsługę MQTT włącza `HAL_ENABLE_MQTT`.

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/08_mqtt --assembly picow
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
