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

| Sygnał po stronie mikrokontrolera RP | Pin / ustawienie |
|---|---|
| UART TX, do RX modemu | GP4 |
| UART RX, z TX modemu | GP5 |
| Sterowanie włączaniem modemu | GP6 |
| Port szeregowy | `HAL_UART_PORT_2`, 115200 baud, 8N1 |

Dobierz zasilanie i poziomy logiczne zgodnie z dokumentacją konkretnego modułu.
Kod używa impulsu sterującego 1500 ms i czeka 15 s po przełączeniu zasilania.
Nie zmieniaj tych wartości bez sprawdzenia wymagań sprzętu.

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/03_modem_A7670E --target rp2040 --board pico
```

Błędy uruchomienia modemu i MQTT są zgłaszane w konsoli; jednakże przykład nie
implementuje pełnej odporności na możliwe błędy transmisji / komunikacji.
