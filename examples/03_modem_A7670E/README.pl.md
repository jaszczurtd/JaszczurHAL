# 03 - Połączenie MQTT przez modem SIMCom A7670

Przykład łączy modem z siecią komórkową, uruchamia połączenie MQTT przez TLS
oraz wysyła i odbiera wiadomości. Korzysta z obsługi poleceń AT dla rodziny
SIMCom A76xx, włączanej flagą `HAL_ENABLE_A7670`.

Po uruchomieniu czeka na gotowość karty SIM i rejestrację w sieci, konfiguruje
APN i subskrybuje `dpf/cmd`. Co 10 sekund podejmuje próbę wysłania
`{"hello":"world"}` do `dpf/data`. Wiadomość `modem_reset` odebrana
w temacie poleceń zleca przełączenie zasilania modemu i ponowną inicjalizację
połączenia.

## Przygotowanie modemu

W `app.c` ustaw `APN`, adres i port brokera, identyfikator klienta, dane
logowania oraz tematy MQTT. `SSL_CA_CERT` wskazuje nazwę `ca.pem`; kod nie
przesyła tego pliku do modemu. Przygotuj certyfikat wymagany przez jego
konfigurację TLS.

Ustawienia TLS w przykładzie obejmują `ignore_local_time = true` i
`enable_sni = false`. Nie traktuj ich jako gotowej konfiguracji dla dowolnego
brokera; sprawdź wymagania serwera i używanego modemu.

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze port
szeregowy, jego ustawienia i pin sterowania włączaniem z wygenerowanego
nagłówka. Poniższa tabela powtarza te połączenia.

| Sygnał po stronie mikrokontrolera | Pico / Pico 2 | ESP32-S3-Zero |
|---|---|---|
| UART TX, do RX modemu | GP4 | GPIO4 |
| UART RX, z TX modemu | GP5 | GPIO5 |
| Sterowanie włączaniem modemu, aktywne stanem niskim | GP6 | GPIO6 |

Na każdej płytce modem pracuje na porcie `HAL_UART_PORT_2` z prędkością
115200 baud, 8N1.

Dobierz zasilanie i poziomy logiczne zgodnie z dokumentacją konkretnego modułu.
Kod używa impulsu sterującego 1500 ms i czeka 15 s po przełączeniu zasilania.
Nie zmieniaj tych wartości bez sprawdzenia wymagań sprzętu.

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/03_modem_A7670E --assembly pico
```

Pozostałe montaże z `device_tree.json` obejmują Pico 2 (ARM i RISC-V)
oraz ESP32-S3-Zero.

Błędy uruchomienia modemu i MQTT są zgłaszane w konsoli; jednakże przykład nie
implementuje pełnej odporności na możliwe błędy transmisji / komunikacji.
