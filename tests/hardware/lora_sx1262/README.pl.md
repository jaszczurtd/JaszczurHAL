# Testy sprzętowe LoRa SX1262

Katalog obsługuje dwie procedury: surowe pakiety między dwoma radiami oraz router poleceń na niezawodnym łączu LoRa.

## Surowe pakiety

`tests/hardware/lora_sx1262` używa możliwego do zbudowania firmware'u
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) oraz
dwóch modułów radiowych pracujących w tym samym paśmie. Sprawdza inicjalizację,
dwukierunkowe przesyłanie pakietów drogą radiową, ciągłość numerów sekwencji,
asynchroniczne callbacki wyzwalane przez DIO1, diagnostykę IRQ i anulowania,
metadane RSSI/SNR, usypianie i wybudzanie oraz ponowną inicjalizację po
zniszczeniu i utworzeniu obiektu radia.

Sam pozytywny wynik nie dowodzi jeszcze, że przerwanie DIO1 się zgłasza: provider
odpytuje także poziom DIO1, więc link działa nawet przy martwym przerwaniu. Żeby
odizolować przerwanie, zbuduj wariant bez odpytywania poziomu i sprawdź, czy link
nadal chodzi.

W profilach ze sprzętową diodą stanu jej ciągłe świecenie oznacza
aktywność nadawania, a impuls 120 ms potwierdza odebrany pakiet.

Nie paruj urządzenia LF z urządzeniem HF. Potwierdź etykiety na obu
radiostacjach i antenach, podłącz właściwą antenę przed włączeniem zasilania,
użyj I/O 3,3 V i przestrzegaj lokalnych przepisów dotyczących widma, mocy i
cyklu pracy.

### Para LF: dwie płytki RP2040-LoRa-LF

Skompiluj i wgraj inicjatora na pierwszą płytkę, następnie skompiluj i wgraj
respondera na drugą płytkę:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf \
  --port /dev/serial/by-id/<lf-initiator>

vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder \
  --port /dev/serial/by-id/<lf-responder>
```

Firmware celowo używa w tym teście częstotliwości 434,0 MHz. Jest to
konfiguracja testowa, a nie uniwersalne ustawienie zgodne z przepisami każdego
regionu.

### Para HF: zewnętrzny Core1262-HF na RP2040 i STM32G474

Użyj stałego okablowania udokumentowanego przez profile złożone. Skompiluj
RP2040/Pico jako inicjatora oraz NUCLEO-G474RE jako respondera (lub odwróć
obie role):

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico-core1262-hf
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re-core1262-hf --variant responder
```

Oba urządzenia Core1262-HF używają tego samego profilu elektrycznego modułu i
konfiguracji technicznej EU868. Mapowania pinów hosta pochodzą z danych
wygenerowanych dla płytek; przykład nie zawiera okablowania zależnego od
układu docelowego. Profil Nucleo używa SPI2 na PB13/PB14/PB15 i pozostawia
LD2/`HAL_LED_BUILTIN` na PA5.

Przed próbą transmisji radiowej można zbudować i wgrać na dowolnym hoście
wariant bez nadawania, wybierając `--variant probe`. Pomyślny wynik potwierdza
możliwości backendu, jawną kalibrację, bieżący poziom RSSI, CAD i tryb czuwania,
bez uruchamiania toru nadawczego RF.

### Weryfikacja

Uruchom wystarczająco długo, aby zaobserwować automatyczne sondy cyklu życia
przy sekwencji 10 i 20:

```bash
python3 tests/hardware/lora_sx1262/verify_pair.py \
  --initiator-port /dev/serial/by-id/<initiator> \
  --responder-port /dev/serial/by-id/<responder> \
  --duration 75
```

Do zaliczenia potrzeba co najmniej pięciu zgodnych sekwencji ping/pong, metadanych
pakietów, znacznika asynchronicznej pętli zdarzeń na obu radiostacjach,
niezerowych liczników IRQ/callback/anulowania, `HAL_OK` snu/wybudzenia oraz
`HAL_OK` reinicjalizacji. Następnie zamień, które fizyczne urządzenie
otrzymuje wariant `responder`, i powtórz test.

Powtórz test dla dwóch deterministycznych kombinacji. Warianty
bazowy i `responder` używają SF9/10 dBm; `sf7` i `responder-sf7` używają
SF7/6 dBm. Nie zakładaj, że SF12/14 dBm jest dozwolone. Oba końce przebiegu
muszą używać pasującej rodziny wariantów. Zarejestruj etykiety
modułu/anteny, dokładne okablowanie, wersję firmware, odległość, liczby
pakietów, straty, zakres RSSI/SNR oraz JSON weryfikatora w prywatnym
raporcie sprzętowym.

## Router poleceń przez LoRa

Warianty `link` i `link-responder` przykładu
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) dołączają
`hal_lora_commands` do jednego niezawodnego łącza. Inicjator wysyła
500-bajtowe binarne żądanie `echo` z identyfikatorem korelacji. Responder
przekazuje je przez wspólny router i zwraca identyczny ładunek. Przy domyślnych
limitach transmisja w każdym kierunku wymaga trzech niezaszyfrowanych
fragmentów łącza.

Zasady obsługi zezwalają zarówno na źródło `LORA_LINK`, jak i `BLE_STREAM`.
Ten test dostarcza tylko adapter LoRa. Wpis dotyczący źródła BLE pokazuje, że
trasę i protokół można w przyszłości wykorzystać również w adapterze BLE; nie
oznacza to, że taki adapter już zaimplementowano.

Dla dwóch zintegrowanych płytek LF, skompiluj i wgraj przeciwne role. Gdy obie
płytki są już w BOOTSEL i dlatego nie mają portu szeregowego, wybierz każdy
dysk jawnie:

```bash
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant link \
  --bootsel-volume /dev/<initiator-partition>

vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant link-responder \
  --bootsel-volume /dev/<responder-partition>
```

Gdy oba urządzenia zgłoszą przez USB interfejs CDC, użyj stabilnych ścieżek
`/dev/serial/by-id/` i przechwyć co najmniej trzy kompletne transakcje:

```bash
python3 tests/hardware/lora_sx1262/verify_commands.py \
  --initiator-port /dev/serial/by-id/<command-initiator> \
  --responder-port /dev/serial/by-id/<command-responder> \
  --duration 75 --minimum-transactions 3
```

Do zaliczenia potrzebny jest oczekiwany znacznik roli z obu urządzeń oraz brak
błędu lub przekroczenia czasu `JHCMD1`. W przypadku co najmniej trzech
niezerowych identyfikatorów żądania dane inicjatora, funkcji obsługi respondera
i odpowiedzi odbieranej przez inicjator muszą być zgodne pod względem długości
500 bajtów, CRC-32 oraz liczby trzech fragmentów. Identyfikator peera funkcji
obsługi i źródło odpowiedzi muszą wynosić odpowiednio `0x1001` i `0x1002`.
Oba identyfikatory sesji muszą odpowiadać niezerowej wartości `READY` właściwej
dla swojej roli. Flagi bezpieczeństwa w niezaszyfrowanych danych muszą wynosić
zero, RSSI musi mieścić się w ujemnym zakresie LoRa, a SNR - w ustalonych
granicach. Źródłem wywołania funkcji obsługi musi być `LORA_LINK`, natomiast
licznik jej wywołań musi ściśle rosnąć. Końcowy status i porównanie bajtów muszą
zakończyć się powodzeniem. Zapisane logi można sprawdzić za pomocą
`--initiator-log` i `--responder-log` zamiast portów szeregowych na żywo.

Zamień role dwóch fizycznych urządzeń i powtórz. Zarejestruj ich etykiety
modułu i anteny, wersję firmware, odległość, dopasowane identyfikatory
żądań, zakres RSSI/SNR oraz JSON weryfikatora tylko w prywatnym raporcie
sprzętowym. Częstotliwość 434,0 MHz jest techniczną wartością testową;
podłącz anteny LF i przestrzegaj lokalnych wymogów dotyczących widma, mocy i
cyklu pracy.
