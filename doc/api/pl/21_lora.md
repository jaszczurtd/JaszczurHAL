<a id="niskopoziomowe-api-radia-lora"></a>

# LoRa - konfiguracja radia i wymiana pakietów

*Dostępne również [po angielsku](../en/21_lora.md).*

> **Część [dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Wysyłanie i odbiór pakietów LoRa przez wspólne API `hal_lora_radio` dla układów SX1261/SX1262 oraz SX1276/SX1278. Kod można skompilować dla RP2040, RP2350 i STM32G474; zakres testów na rzeczywistym sprzęcie podano niżej. Deterministyczna implementacja mock służy do testów hostowych.

SX1261/SX1262 korzystają z oficjalnego sterownika Semtech SX126x w wersji ustalonej w repozytorium. SX1276/SX1278 używają sterownika rejestrowego utrzymywanego w HAL. Obie implementacje zależą wyłącznie od usług SPI, GPIO, czasu i synchronizacji JaszczurHAL.

API udostępnia blokujące i asynchroniczne nadawanie, asynchroniczny odbiór, wykrywanie aktywności kanału (CAD), bieżące RSSI, jawną kalibrację oraz informacje o limitach i dostępnych funkcjach. Aplikacja może odbierać zdarzenia, sprawdzać stan i anulować operacje. Obsługa zdarzeń DIO odbywa się w zadaniu, nie w przerwaniu.

Aplikacja dostarcza i przechowuje deskryptory sprzętu i modemu. Każdy nieprzezroczysty uchwyt ma własne bufory TX/RX, stan, muteks i diagnostykę.

## Włączanie modułu

Wybierz dokładnie jedną rodzinę układów w `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_SX126X
/* or: #define HAL_ENABLE_SX127X */
```

Rejestr flag automatycznie włącza `HAL_ENABLE_LORA` i `HAL_ENABLE_SPI`.
Konfiguracja zawierająca samo `HAL_ENABLE_LORA` albo obie rodziny jednocześnie
jest odrzucana.

Przed dołączeniem `hal_config.h` można ustawić następujące parametry:

| Makro | Domyślnie | Dozwolony zakres | Przeznaczenie |
|---|---:|---:|---|
| `HAL_LORA_RADIO_MAX_INSTANCES` | 2 | 1..255 | Liczba miejsc w statycznej puli uchwytów oznaczonych numerem generacji |
| `HAL_LORA_SX126X_BUSY_TIMEOUT_MS` | 1000 | 1..60000 | Maksymalny czas oczekiwania na linię BUSY SX126x przy wykonywaniu polecenia |
| `HAL_LORA_SX127X_RESET_SETTLE_MS` | 10 | 5..1000 | Opóźnienie od zwolnienia resetu SX127x do sprawdzenia jego wersji |

<a id="dojrzałość-modeli"></a>

## Zakres weryfikacji poszczególnych układów

SX1262 sprawdzono na fizycznych płytkach i stanowiskach opisanych niżej. SX1261, SX1276 i SX1278 pozostają `experimental`: przeszły deterministyczne testy hostowe oraz kompilację i linkowanie dla RP2040 i STM32G474, lecz nie testy z fizycznym radiem. Nie mają zatem profili płytek ani informacji o dostępności funkcji podczas pracy. Zmiana tego statusu wymaga udokumentowanego testu sprzętowego konkretnego modelu.

Sterowniki Semtech SX127x z LoRaMac-node i LoRa Basics Modem zależą od warstw płytki, timerów i stosu tych projektów. JaszczurHAL nie dołącza całego takiego stosu do bezpośredniej obsługi radia. Zamiast tego używa niewielkiego sterownika opartego na publicznie opisanych rejestrach SX127x, dostępnego przez to samo API co SX126x.

## Obsługa i współdzielenie sprzętu

Podczas tworzenia radia wybrany kontroler SPI jest inicjalizowany z pinami
podanymi w deskryptorze. Implementacja chroni każde polecenie blokadą
magistrali HAL, a dla każdej transakcji ustawia częstotliwość zegara urządzenia,
tryb 0 i kolejność MSB-first. Steruje też pinami CS i RESET oraz pozostałymi
liniami właściwymi dla danej rodziny, zgodnie z deskryptorem sprzętu. Dla
SX126x są to BUSY, DIO1 i układ przełącznika RF/TCXO. SX127x ma osobny
deskryptor dla DIO0-DIO2, opcjonalnych GPIO przełącznika RX/TX, opcjonalnego
włączenia TCXO oraz wyboru RFO lub PA_BOOST.

Magistralę SPI można współdzielić z innymi urządzeniami HAL. Tworzenie radia rejestruje przerwania zbocza narastającego, których obsługa tylko zaznacza oczekującą pracę. Polecenia SPI i callbacki są wykonywane później w zadaniu. Zniszczenie radia odłącza właściwe linie DIO, przełącza układ w bezpieczny stan zasilania i zwalnia uchwyt, ale nie wyłącza współdzielonej magistrali.

## Konfiguracja z profilu płytki

`hal_lora_radio_config_from_board()` kopiuje konfigurację radia z aktywnego
profilu płytki. Zwraca `HAL_EUNSUPPORTED`, jeśli profil nie deklaruje radia
zintegrowanego ani będącego częścią stałego stanowiska.

```c
hal_lora_radio_config_t hardware;
hal_status_t status = hal_lora_radio_config_from_board(&hardware);
if (status != HAL_OK) {
  return status;
}

status = hal_spi_init(hardware.spi_bus, hardware.spi_miso_pin,
                      hardware.spi_mosi_pin, hardware.spi_sck_pin);
```

Profil `rp2040-lora-lf` opisuje zintegrowany SX1262 na płytce Waveshare
RP2040-LoRa-LF, w tym limity częstotliwości pasma LF. Dobierz częstotliwość
jawnie do używanego sprzętu, scenariusza testowego i lokalnych przepisów.

Eksperymentalne profile `pico-core1262-hf` i
`nucleo-g474re-core1262-hf` opisują dwie stałe konfiguracje testowe projektu
z zewnętrznymi modułami Waveshare Core1262-HF. Oba przeszły testy CAD, RSSI
i kalibracji bez transmisji oraz dwukierunkowe testy radiowe. Stanowisko Nucleo
używa SPI2 na
PB13/PB14/PB15, dzięki czemu wbudowana LD2 i `HAL_LED_BUILTIN` pozostają
dostępne na PA5.

## Zewnętrzny moduł Waveshare Core1262-HF

Dla stałego okablowania używanego w projekcie wybierz
`pico-core1262-hf` albo `nucleo-g474re-core1262-hf` i użyj
`hal_lora_radio_config_from_board()`. Jeśli aplikacja używa innego okablowania,
wybierz zwykły profil hosta i użyj
`hal_lora_sx126x_core1262_hf_defaults()`. Funkcja wypełnia parametry elektryczne
modułu: podwójne sterowanie RXEN/TXEN, DCDC, TCXO 1,8 V sterowane przez DIO3,
opóźnienie startu, zakres RF, limit SPI i limity mocy wyjściowej. Aplikacja
nadal musi podać magistralę oraz przypisanie pinów hosta. Przypisz RXEN do
`rf_switch_pin_a`, a TXEN do `rf_switch_pin_b`; funkcja ustawi udokumentowaną
tabelę poziomów.

```c
hal_lora_radio_config_t hardware = {0};
hardware.model = HAL_LORA_RADIO_SX1262;
hardware.spi_bus = 0;
hardware.spi_miso_pin = 16;
hardware.spi_mosi_pin = 19;
hardware.spi_sck_pin = 18;
hardware.cs_pin = 17;
hardware.spi_clock_hz = HAL_LORA_SPI_CLOCK_DEFAULT_HZ;

hal_lora_sx126x_hardware_config_t *sx = &hardware.hardware.sx126x;
hal_status_t status = hal_lora_sx126x_core1262_hf_defaults(sx);
if (status != HAL_OK) {
  return status;
}
sx->reset_pin = 20;
sx->busy_pin = 21;
sx->dio1_pin = 22;
sx->rf_switch_pin_a = 10;
sx->rf_switch_pin_b = 11;
```

`HAL_LORA_PIN_NONE` oznacza celowo niepodłączony GPIO. Wymagane piny i
topologia przełącznika RF są sprawdzane w `hal_lora_radio_create()`.

Jeśli aplikacja określa okablowanie SX127x, wypełnij bezpośrednio
`hardware.sx127x`. DIO0 i RESET są wymagane; DIO1/DIO2, GPIO przełącznika RX/TX
oraz włączenie TCXO mogą używać `HAL_LORA_PIN_NONE`. Wybrane wyjście PA
ogranicza dozwolony zakres mocy: RFO obsługuje -4..15 dBm, a PA_BOOST 2..20 dBm.
SX1278 jest ograniczony do 137..525 MHz, natomiast deskryptor SX1276 może
sięgać do 960 MHz. Parametry modułu mogą dodatkowo zawęzić zakres układu.

```c
hal_lora_radio_config_t hardware = {0};
hardware.model = HAL_LORA_RADIO_SX1276;
hardware.spi_bus = 0;
hardware.spi_miso_pin = 16;
hardware.spi_mosi_pin = 19;
hardware.spi_sck_pin = 18;
hardware.cs_pin = 17;
hardware.spi_clock_hz = UINT32_C(4000000);
hardware.hardware.sx127x.reset_pin = 20;
hardware.hardware.sx127x.dio0_pin = 21;
hardware.hardware.sx127x.dio1_pin = 22;
hardware.hardware.sx127x.dio2_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.rf_switch_rx_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.rf_switch_tx_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.tcxo_enable_pin = HAL_LORA_PIN_NONE;
hardware.hardware.sx127x.pa_output = HAL_LORA_SX127X_PA_BOOST;
hardware.hardware.sx127x.min_frequency_hz = UINT32_C(850000000);
hardware.hardware.sx127x.max_frequency_hz = UINT32_C(930000000);
hardware.hardware.sx127x.max_spi_clock_hz = UINT32_C(10000000);
hardware.hardware.sx127x.min_tx_power_dbm = 2;
hardware.hardware.sx127x.max_tx_power_dbm = 20;
```

<a id="cykl-życia-i-konfiguracja-modemu"></a>

## Uruchamianie i konfiguracja modemu

Przygotuj SPI, utwórz uchwyt i sprawdź radio, skonfiguruj modem, a następnie obsługuj pakiety. Na końcu zwolnij radio:

```c
hal_lora_radio_t radio = NULL;
hal_status_t status = hal_lora_radio_create(&hardware, &radio);
if (status != HAL_OK) {
  return status;
}

hal_lora_modem_config_t modem = hal_lora_default_eu868();
status = hal_lora_radio_configure(radio, &modem);
if (status != HAL_OK) {
  (void)hal_lora_radio_destroy(radio);
  return status;
}
```

Każdy uchwyt zawiera numer generacji. Użycie uchwytu zniszczonego radia albo
innego nieaktualnego uchwytu powoduje zwrócenie `HAL_EUNINIT`. Gdy
skonfigurowana pula statyczna jest pełna, `hal_lora_radio_create()` zwraca
`HAL_ENOMEM`. Przed zniszczeniem radia trzeba zakończyć lub anulować aktywne
TX, RX albo CAD.

Walidacja modemu obejmuje limity częstotliwości i mocy modelu oraz modułu,
obsługiwane szerokości pasma LoRa, coding rate od 5 do 8, preambułę, tryb
nagłówka i długość danych w trybie implicit. SX126x obsługuje spreading
factor od 5 do 12, a SX127x od 6 do 12.

Trzy gotowe konfiguracje EU868 używają 868,1 MHz, 125 kHz, coding rate 4/5,
jawnego nagłówka, CRC, preambuły ośmiosymbolowej i mocy 14 dBm:

| Funkcja | Spreading factor | Sugerowany punkt wyjścia |
|---|---:|---|
| `hal_lora_default_fast_eu868()` | SF7 | Krótki czas transmisji |
| `hal_lora_default_eu868()` | SF9 | Zrównoważony zasięg |
| `hal_lora_default_long_range_eu868()` | SF12 | Dłuższy czas transmisji i większy budżet łącza |

To wyłącznie techniczne konfiguracje początkowe. Aplikacja musi dobrać częstotliwość, moc, antenę, szerokość pasma i współczynnik zajętości pasma (duty cycle) do lokalnych przepisów.

Urządzenie LF wymaga jawnej konfiguracji zamiast jednej z gotowych konfiguracji:

```c
hal_lora_modem_config_t modem = hal_lora_default_eu868();
modem.frequency_hz = UINT32_C(434000000);
modem.tx_power_dbm = 10;
```

## Transmisja blokująca

`hal_lora_radio_transmit()` kopiuje od 1 do 255 bajtów do pamięci powiązanej
z uchwytem, rozpoczyna TX i czeka na zakończenie transmisji albo timeout.
Wartość timeoutu równa zero wybiera automatyczny limit czasu obliczony na
podstawie czasu transmisji pakietu i wewnętrznego marginesu.

```c
static const uint8_t payload[] = "ping";
status = hal_lora_radio_transmit(radio, payload, sizeof(payload) - 1u, 0u);
```

Po udanej operacji lub po upływie timeoutu radio wraca do stanu standby. Błąd
magistrali albo urządzenia przełącza je do `HAL_LORA_RADIO_STATE_ERROR`.
Pomyślne wywołanie `hal_lora_radio_configure()` może przywrócić skonfigurowany
stan standby.

Wywołanie blokujące obsługuje tę samą maszynę stanów co asynchroniczna transmisja uruchamiana i przetwarzana osobnymi funkcjami. Kopiuje dane, więc nie zachowuje wskaźnika do bufora aplikacji.

## Operacje asynchroniczne i funkcje zwrotne

`hal_lora_radio_transmit_start()` kopiuje dane, uruchamia radio i kończy
działanie przed zakończeniem TX. Limit czasu jest obliczany na podstawie
czasu transmisji pakietu i marginesu sterownika. Wywołuj
`hal_lora_radio_process()` z `app_task0()` lub zadania FreeRTOS i
odczytuj spójny stan operacji:

```c
static void radio_event(hal_lora_radio_t radio,
                        const hal_lora_radio_event_t *event,
                        void *user_data) {
  (void)radio;
  (void)user_data;
  if (event->type == HAL_LORA_RADIO_EVENT_TX_COMPLETE) {
    /* Start RX or schedule the next packet. */
  }
}

status = hal_lora_radio_set_event_callback(radio, radio_event, NULL);
if (status == HAL_OK) {
  status = hal_lora_radio_transmit_start(radio, payload, payload_length);
}

for (;;) {
  status = hal_lora_radio_process(radio);
  if (status != HAL_OK && status != HAL_EAGAIN) {
    /* The callback receives the same terminal result. */
  }

  hal_lora_operation_status_t tx;
  if (hal_lora_radio_get_tx_status(radio, &tx) == HAL_OK &&
      tx.state == HAL_LORA_OPERATION_SUCCEEDED) {
    break;
  }
  hal_idle();
}
```

Status TX rozróżnia `IDLE`, `IN_PROGRESS`, `SUCCEEDED`, `TIMED_OUT`,
`CANCELLED` i `FAILED`; pole `result` zawiera odpowiadający im `hal_status_t`.
Funkcje zwrotne sygnalizują zakończenie TX, gotowość RX, zakończenie CAD,
timeout, anulowanie lub błąd. Przekazują również rodzaj operacji oraz znacznik
czasu pobrany w kontekście zadania. Zdarzenie zakończenia CAD zawiera też
`channel_activity_detected`. Funkcje zwrotne są
wywoływane synchronicznie przez `hal_lora_radio_process()`, poza wewnętrznymi
blokadami i mogą wywoływać API radia. Przekazanie `NULL` jako funkcji zwrotnej
usuwa rejestrację.

`hal_lora_radio_cancel()` zatrzymuje aktywne TX, RX z timeoutem, ciągłe RX lub
CAD i przełącza radio do standby. Anulowanie jest jawne: próba zmiany stanu
zasilania lub zniszczenia radia zwraca `HAL_EBUSY`, gdy trwa inna operacja.

<a id="odbiór-przez-polling"></a>

## Odbiór i okresowe odpytywanie

Rozpocznij odbiór z limitem czasu, regularnie przetwarzaj oczekujące zdarzenia radia i skopiuj gotowy pakiet:

```c
status = hal_lora_radio_receive_start(radio, 1500u);
while (status == HAL_OK) {
  const hal_status_t process = hal_lora_radio_process(radio);
  if (process != HAL_OK && process != HAL_EAGAIN) {
    status = process;
    break;
  }
  uint8_t packet[HAL_LORA_RADIO_MAX_PAYLOAD];
  size_t length = 0;
  hal_lora_packet_info_t info;
  status = hal_lora_radio_receive(radio, packet, sizeof(packet),
                                  &length, &info);
  if (status == HAL_EAGAIN) {
    hal_idle();
    continue;
  }
  if (status == HAL_OK) {
    /* packet[0..length), info.rssi_dbm, info.snr_db */
  }
  break;
}
```

`hal_lora_radio_receive_start_continuous()` pozostawia odbiornik aktywny po
odebraniu pakietu. Aby zatrzymać ciągły odbiór, wywołaj
`hal_lora_radio_cancel()`. `hal_lora_radio_receive()` zachowuje zgodność
z dotychczasowym interfejsem pollingu. Przed skopiowaniem pakietu samodzielnie
obsługuje jedno oczekujące przerwanie lub timeout providera.

Znaczenie wyników odbioru:

| Status | Znaczenie |
|---|---|
| `HAL_OK` | Skopiowano jeden pakiet i jego metadane |
| `HAL_EAGAIN` | RX nadal trwa |
| `HAL_ETIMEOUT` | Upłynął timeout odbioru; radio wróciło do standby |
| `HAL_EPROTO` | Radio zgłosiło błąd CRC |
| `HAL_EOVERFLOW` | Pakiet został odebrany i usunięty z kolejki, `out_length` zawiera jego pełny rozmiar, a bufor wywołującego zawiera mieszczący się prefiks |

`hal_lora_packet_info_t` zawiera RSSI pakietu, SNR, RSSI sygnału, znacznik czasu
odbioru i informację o poprawności CRC.

<a id="parametry-i-funkcje-sprzętu-bieżący-rssi-cad-i-kalibracja"></a>

## Dostępne funkcje, RSSI, CAD i kalibracja

`hal_lora_radio_get_capabilities()` zwraca limity sprzętowe i dostępne operacje
opcjonalne w formie niezależnej od rodziny radia. SX126x obsługuje ciągły odbiór,
CAD, odczyt bieżącego RSSI i jawną kalibrację. SX127x obsługuje ciągły odbiór,
CAD i bieżący RSSI, natomiast jawna kalibracja jest niedostępna:

```c
hal_lora_radio_capabilities_t capabilities;
status = hal_lora_radio_get_capabilities(radio, &capabilities);
```

Wywołuj `hal_lora_radio_calibrate()` tylko wtedy, gdy
`supports_explicit_calibration` jest ustawione. SX127x zwraca
`HAL_EUNSUPPORTED` bez opuszczania stanu standby.

`hal_lora_radio_get_instant_rssi()` odczytuje bieżący RSSI odbiornika. Funkcję
można wywoływać tylko w trybie RX. W stanie standby, TX, CAD lub sleep zwraca
`HAL_ESTATE`. Odczyt aktualizuje pola diagnostyczne `last_instant_rssi_dbm` i
`instant_rssi_reads`.

CAD jest operacją asynchroniczną przetwarzaną przez ten sam mechanizm obsługi
DIO co TX i RX:

```c
status = hal_lora_radio_channel_activity_detect_start(radio, 100u);
while (status == HAL_OK) {
  const hal_status_t process = hal_lora_radio_process(radio);
  if (process != HAL_OK && process != HAL_EAGAIN) {
    status = process;
    break;
  }

  hal_lora_channel_activity_status_t cad;
  status = hal_lora_radio_get_channel_activity_status(radio, &cad);
  if (status == HAL_OK && cad.state == HAL_LORA_OPERATION_IN_PROGRESS) {
    hal_idle();
    continue;
  }
  if (status == HAL_OK && cad.state == HAL_LORA_OPERATION_SUCCEEDED) {
    /* cad.detected distinguishes an occupied channel from a clear channel. */
  }
  break;
}
```

Limit czasu CAD nie może być równy zero. Upływ limitu, anulowanie i błędy
providera zmieniają stan operacji i wywołują funkcje zwrotne na tych samych
zasadach. CAD jedynie sprawdza aktywność kanału; nie implementuje wymaganej
przepisami procedury listen-before-talk. Za zastosowanie takiej procedury
odpowiada aplikacja.

Provider SX126x wykonuje pełną kalibrację podczas tworzenia radia, a podczas
konfiguracji również kalibrację obrazu właściwą dla wybranego pasma. Zapamiętuje
skalibrowany zakres częstotliwości, dlatego ponowna konfiguracja w tym samym
zakresie nie uruchamia niepotrzebnie kolejnej kalibracji obrazu.
`hal_lora_radio_calibrate()` jawnie powtarza pełną
kalibrację i kalibrację obrazu dla skonfigurowanej częstotliwości w stanie
standby. Przed konfiguracją modemu lub w czasie innej aktywnej operacji zwraca
`HAL_ESTATE`.
SX127x zgłasza brak obsługi jawnej kalibracji, a próba jej wykonania zwraca
`HAL_EUNSUPPORTED`.

## Stan, zasilanie i diagnostyka

`hal_lora_radio_get_state()` zwraca bieżący, stabilny stan widoczny przez API.
Maszyna stanów używa wartości `STANDBY`, `RX`, `TX`, `CAD`, `SLEEP` i `ERROR`.

`hal_lora_radio_sleep()` usypia wybrane radio. `hal_lora_radio_standby()`
wybudza radio ze stanu sleep albo error. Aktywne RX lub TX trzeba wcześniej
zakończyć przez `hal_lora_radio_cancel()`.

`hal_lora_radio_get_diagnostics()` zwraca kopię liczników wysłanych i odebranych
pakietów, błędów CRC/nagłówka, timeoutów TX/RX, anulowań, błędów operacji i
magistrali, zdarzeń DIO, wywołań funkcji zwrotnych, odrzuconych
pakietów/zdarzeń i resetów. Liczniki CAD rozróżniają próby sprawdzenia, kanał
zajęty, kanał wolny i timeout.
Liczniki kalibracji rozróżniają kalibrację pełną i obrazu oraz zachowują
zapamiętany zakres częstotliwości. Diagnostyka zwraca również ostatnie RSSI
pakietu, RSSI sygnału, bieżące RSSI, SNR, błąd oraz znaczniki czasu zdarzenia
i zmiany stanu.

## Czas transmisji pakietu

`hal_lora_time_on_air()` sprawdza pola modulacji LoRa i pakietu oraz zwraca
zaokrąglony w górę czas trwania pakietu w milisekundach. Limity częstotliwości
i mocy wyjściowej zależą od sprzętu i sprawdza je
`hal_lora_radio_configure()`:

```c
uint32_t airtime_ms = 0;
status = hal_lora_time_on_air(&modem, 32u, &airtime_ms);
```

Obliczony czas transmisji wykorzystuj do dobierania jawnego timeoutu TX
i obliczania duty cycle zgodnego z przepisami.

<a id="współbieżność-i-walidacja"></a>

## Współbieżność i testy

Każdy uchwyt osobno synchronizuje wywołania wykonywane podczas pracy. Tworzenie i niszczenie (`create`, `destroy`) wykonuj z jednego kontekstu i tego samego rdzenia, który obsługuje przerwania GPIO radia. Funkcja rozpoczynająca operację kopiuje pakiet przed powrotem. Callbacki są wykonywane bez blokady uchwytu.

Testy hosta znajdują się w `test_hal_lora_radio_lifecycle`,
`test_hal_lora_radio`, `test_hal_lora_sx127x`, `test_sx126x_adapter`,
`test_sx127x_adapter` oraz w `test_lora_freertos_posix`, który działa
z rzeczywistym schedulerem. Przykład
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) można
zbudować dla RP2040 i STM32G474. Powtarzalna procedura dla dwóch urządzeń
i weryfikator portu szeregowego są opisane w
[teście sprzętowym niskopoziomowej komunikacji LoRa SX1262](../../../tests/hardware/lora_sx1262/README.pl.md).
