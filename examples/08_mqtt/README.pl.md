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
  --project examples/08_mqtt --target rp2040 --board picow
```

Domyślne płytki to `picow` dla RP2040, `pico2w` dla RP2350 ARM
oraz `nucleo-g474re-pim730` dla STM32G474 z zewnętrznym modułem PIM730/RM2.
Projekt nie obejmuje RP2350 RISC-V.
