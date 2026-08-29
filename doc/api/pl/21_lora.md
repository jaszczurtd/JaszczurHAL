# API surowej komunikacji radiowej LoRa

*Dostępne również [po angielsku](../en/21_lora.md).*

> **Część [dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

`hal_lora_radio` jest niezależną od providera fasadą surowej komunikacji
pakietowej. Wybrany provider rodziny obsługuje SX1261/SX1262 przez przypięty,
oficjalny driver Semtech SX126x albo SX1276/SX1278 przez należący do HAL driver
rejestrów SX127x. Obie implementacje używają wyłącznie usług HAL dla SPI,
GPIO, pomiaru czasu i muteksów oraz budują się dla RP2040, RP2350 i STM32G474.
Deterministyczny mock umożliwia testowanie na hoście.

API obsługuje blokującą i asynchroniczną transmisję, asynchroniczny odbiór,
przetwarzanie zdarzeń DIO w kontekście taska, wykrywanie aktywności kanału
(CAD), odczyt bieżącego RSSI, jawną kalibrację, capabilities, callbacki,
anulowanie i jawne stany operacji. Aplikacja jest właścicielem deskryptorów
sprzętu i modemu. Każdy nieprzezroczysty uchwyt ma własne bufory pakietów TX/RX,
stan, muteks i diagnostykę.

## Włączanie modułu

Wybierz dokładnie jednego providera rodziny w `hal_project_config.h`:

```c
#pragma once

#define HAL_ENABLE_SX126X
/* or: #define HAL_ENABLE_SX127X */
```

Rejestr funkcji propaguje `HAL_ENABLE_LORA` i `HAL_ENABLE_SPI`.
Samodzielne wybranie `HAL_ENABLE_LORA` albo obu providerów rodziny jest
odrzucane.

Przed dołączeniem `hal_config.h` można ustawić następujące parametry:

| Makro | Domyślnie | Poprawny zakres | Przeznaczenie |
|---|---:|---:|---|
| `HAL_LORA_RADIO_MAX_INSTANCES` | 2 | 1..255 | Liczba statycznych slotów uchwytów oznaczonych generacją |
| `HAL_LORA_SX126X_BUSY_TIMEOUT_MS` | 1000 | 1..60000 | Maksymalny czas oczekiwania na linię BUSY SX126x przy wykonywaniu polecenia |
| `HAL_LORA_SX127X_RESET_SETTLE_MS` | 10 | 5..1000 | Opóźnienie od zwolnienia resetu SX127x do sprawdzenia jego wersji |

## Dojrzałość modeli

SX1262 został sprawdzony na fizycznych płytkach i fixture'ach opisanych niżej.
SX1261, SX1276 i SX1278 mają status `experimental`: ich integracja przeszła
deterministyczne testy hosta oraz bramki buildu i linkowania dla RP2040 i
STM32G474, ale nie był dostępny fizyczny moduł radiowy. Celowo nie dodają
profilu płytki ani capabilities runtime. Awans modelu wymaga udokumentowanego
testu sprzętowego.

Implementacje Semtech SX127x dostępne w LoRaMac-node i LoRa Basics Modem są
powiązane z właściwymi im warstwami płytki, timerów i stosu. Dołączenie całego
stosu wyłącznie po to, aby uzyskać surowy dostęp do rejestrów radia, tworzyłoby
zbędną zależność. Dlatego JaszczurHAL zawiera zwarty provider SX127x, który
korzysta z publicznego interfejsu rejestrów i pozostaje za tą samą fasadą
niezależną od providera.

## Własność sprzętu

Utworzenie radia inicjalizuje wybrany kontroler SPI z pinami z deskryptora.
Fasada serializuje każde polecenie blokadą magistrali HAL i dla każdej
transakcji ustawia zegar urządzenia, tryb 0 oraz kolejność MSB-first. Zarządza
pinami CS i RESET radia oraz zachowaniem sterowania właściwym dla danej rodziny,
opisanym w deskryptorze sprzętu. SX126x zarządza liniami BUSY i DIO1 oraz
topologią przełącznika RF/TCXO. SX127x ma osobny deskryptor dla DIO0-DIO2,
opcjonalnych GPIO przełącznika RX/TX, opcjonalnego włączenia TCXO oraz wyboru
RFO lub PA_BOOST.

Kontroler SPI może być współdzielony z innymi urządzeniami HAL. Provider
podłącza przerwania zbocza narastającego podczas tworzenia radia; ISR zapisuje
jedynie oczekującą pracę. Polecenia SPI i callbacki są wykonywane później w
kontekście taska. Zniszczenie radia odłącza linie DIO właściwe dla rodziny,
przełącza radio w bezpieczny stan zasilania i zwalnia uchwyt bez
deinicjalizowania współdzielonej magistrali.

## Konfiguracja z profilu płytki

`hal_lora_radio_config_from_board()` kopiuje parametry radia z aktywnego profilu
płytki. Zwraca `HAL_EUNSUPPORTED`, jeśli wybrana płytka nie deklaruje radia -
zarówno zintegrowanego, jak i będącego częścią stałego fixture'a.

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
RP2040-LoRa-LF, w tym limity częstotliwości pasma LF. Wybierz częstotliwość
odpowiednią dla używanego sprzętu, testu i lokalnych przepisów.

Eksperymentalne profile `pico-core1262-hf` i
`nucleo-g474re-core1262-hf` opisują dwie stałe konfiguracje testowe projektu z zewnętrznymi
modułami Waveshare Core1262-HF. Oba przeszły testy CAD/RSSI/kalibracji bez
transmisji i dwukierunkowe testy OTA. Fixture Nucleo używa SPI2 na
PB13/PB14/PB15, dzięki czemu wbudowana LD2 i `HAL_LED_BUILTIN` pozostają
dostępne na PA5.

## Zewnętrzny moduł Waveshare Core1262-HF

Dla stałego okablowania używanego w projekcie wybierz
`pico-core1262-hf` albo `nucleo-g474re-core1262-hf` i użyj
`hal_lora_radio_config_from_board()`. Dla innego okablowania należącego do
aplikacji wybierz zwykły profil hosta i użyj
`hal_lora_sx126x_core1262_hf_defaults()`. Helper wypełnia profil elektryczny
modułu: podwójne sterowanie RXEN/TXEN, DCDC, TCXO 1,8 V sterowane przez DIO3,
opóźnienie startu, zakres RF, limit SPI i limity mocy wyjściowej. Przypisanie
magistrali i pinów hosta pozostaje wejściem aplikacji. Przypisz RXEN do
`rf_switch_pin_a`, a TXEN do `rf_switch_pin_b`; helper ustawi udokumentowaną
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

Dla okablowania SX127x należącego do aplikacji wypełnij bezpośrednio
`hardware.sx127x`. DIO0 i RESET są wymagane; DIO1/DIO2, GPIO przełącznika RX/TX
oraz włączenie TCXO mogą używać `HAL_LORA_PIN_NONE`. Wybrana ścieżka PA
ogranicza dozwolony zakres mocy: RFO obsługuje -4..15 dBm, a PA_BOOST 2..20 dBm.
SX1278 jest ograniczony do 137..525 MHz, natomiast deskryptor SX1276 może
sięgać do 960 MHz. Limity modułu mogą dodatkowo zawęzić limity układu.

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

## Cykl życia i konfiguracja modemu

Typowa kolejność to inicjalizacja SPI, utworzenie i sprawdzenie radia,
konfiguracja modemu, operacje na pakietach i zniszczenie radia:

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

Uchwyty zawierają znacznik generacji. Wywołanie przez zniszczony lub w inny
sposób nieaktualny uchwyt zwraca `HAL_EUNINIT`. Gdy skonfigurowana statyczna
pula jest pełna, `hal_lora_radio_create()` zwraca `HAL_ENOMEM`. Przed
zniszczeniem radia trzeba zakończyć lub anulować aktywne TX/RX/CAD.

Walidacja modemu obejmuje limity częstotliwości i mocy modelu oraz modułu,
obsługiwane szerokości pasma LoRa, coding rate od 5 do 8, preambułę, tryb
nagłówka i długość payloadu w trybie implicit. SX126x obsługuje spreading
factor od 5 do 12, a SX127x od 6 do 12.

Trzy techniczne presety EU868 używają 868,1 MHz, 125 kHz, coding rate 4/5,
jawnego nagłówka, CRC, preambuły ośmiosymbolowej i mocy 14 dBm:

| Helper | Spreading factor | Sugerowany punkt wyjścia |
|---|---:|---|
| `hal_lora_default_fast_eu868()` | SF7 | Krótki airtime |
| `hal_lora_default_eu868()` | SF9 | Zrównoważony zasięg |
| `hal_lora_default_long_range_eu868()` | SF12 | Dłuższy airtime i większy link budget |

Te wartości są technicznymi punktami wyjścia. Aplikacja odpowiada za zgodne z
lokalnymi przepisami częstotliwość, moc wyjściową, antenę, szerokość pasma i
duty cycle.

Urządzenie LF używa celowo jawnej konfiguracji zamiast globalnego presetu:

```c
hal_lora_modem_config_t modem = hal_lora_default_eu868();
modem.frequency_hz = UINT32_C(434000000);
modem.tx_power_dbm = 10;
```

## Transmisja blokująca

`hal_lora_radio_transmit()` kopiuje od 1 do 255 bajtów do pamięci uchwytu,
rozpoczyna TX i czeka na zakończenie transmisji albo timeout. Timeout równy zero
wybiera deadline obliczony z airtime pakietu i wewnętrznego marginesu.

```c
static const uint8_t payload[] = "ping";
status = hal_lora_radio_transmit(radio, payload, sizeof(payload) - 1u, 0u);
```

Operacja udana lub zakończona timeoutem przywraca standby. Błąd magistrali lub
urządzenia przełącza radio do `HAL_LORA_RADIO_STATE_ERROR`; udane wywołanie
`hal_lora_radio_configure()` może przywrócić skonfigurowany stan standby.

Funkcja blokująca korzysta z tej samej maszyny stanów start/process co
asynchroniczne TX. Nie zachowuje bufora payloadu wywołującego.

## Operacje asynchroniczne i callbacki

`hal_lora_radio_transmit_start()` kopiuje payload, uruchamia radio i wraca przed
zakończeniem TX. Deadline obliczany jest z time-on-air i marginesu drivera.
Wywołuj `hal_lora_radio_process()` z `app_task0()` lub taska FreeRTOS i
sprawdzaj stabilny snapshot statusu:

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
`CANCELLED` i `FAILED`; pole `result` zachowuje odpowiadający `hal_status_t`.
Callbacki zgłaszają zakończenie TX, gotowość RX, zakończenie CAD, timeout,
anulowanie lub błąd, wraz z rodzajem operacji i timestampem z kontekstu taska.
Zakończenie CAD zawiera też `channel_activity_detected`. Callbacki są
wywoływane synchronicznie przez `hal_lora_radio_process()`, poza wewnętrznymi
blokadami, i mogą wywoływać API radia. Przekazanie pustego callbacku usuwa
rejestrację.

`hal_lora_radio_cancel()` zatrzymuje aktywne TX, ograniczone RX, ciągłe RX lub
CAD i przełącza radio do standby. Anulowanie jest jawne: operacje zmiany stanu
zasilania i niszczenia zwracają `HAL_EBUSY`, gdy operacja radia jest aktywna.

## Odbiór przez polling

Uruchom jedno ograniczone okno odbioru, obsługuj IRQ providera i skopiuj
ukończony pakiet:

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
`hal_lora_radio_cancel()`. `hal_lora_radio_receive()` pozostaje zgodnym
interfejsem pollingu: przed skopiowaniem pakietu sam obsługuje jeden oczekujący
krok IRQ/timeout providera.

Znaczenie wyników odbioru:

| Status | Znaczenie |
|---|---|
| `HAL_OK` | Skopiowano jeden pakiet i jego metadane |
| `HAL_EAGAIN` | RX nadal trwa |
| `HAL_ETIMEOUT` | Upłynęło ograniczone okno odbioru; stan wrócił do standby |
| `HAL_EPROTO` | Radio zgłosiło błąd CRC |
| `HAL_EOVERFLOW` | Pakiet został zużyty, `out_length` zawiera jego pełny rozmiar, a bufor wywołującego zawiera mieszczący się prefiks |

`hal_lora_packet_info_t` zawiera RSSI pakietu, SNR, RSSI sygnału, timestamp
odbioru i informację o poprawności CRC.

## Capabilities, bieżący RSSI, CAD i kalibracja

`hal_lora_radio_get_capabilities()` zwraca niezależne od providera limity
sprzętowe i dostępne operacje opcjonalne. SX126x udostępnia ciągły odbiór, CAD,
bieżący RSSI i jawną kalibrację. SX127x udostępnia ciągły odbiór, CAD i bieżący
RSSI, a jawną kalibrację zgłasza jako nieobsługiwaną:

```c
hal_lora_radio_capabilities_t capabilities;
status = hal_lora_radio_get_capabilities(radio, &capabilities);
```

Wywołuj `hal_lora_radio_calibrate()` tylko wtedy, gdy
`supports_explicit_calibration` jest ustawione. SX127x zwraca
`HAL_EUNSUPPORTED` bez zmiany stabilnego stanu standby.

`hal_lora_radio_get_instant_rssi()` odczytuje bieżący RSSI odbiornika i jest
poprawne tylko w trybie RX. W stanie standby, TX, CAD lub sleep zwraca
`HAL_ESTATE`. Odczyt aktualizuje pola diagnostyczne `last_instant_rssi_dbm` i
`instant_rssi_reads`.

CAD jest operacją asynchroniczną obsługiwaną przez ten sam cykl DIO/process co
TX i RX:

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

Zerowy timeout CAD jest nieprawidłowy. Timeout, anulowanie i błędy providera
korzystają ze wspólnej semantyki stanu operacji i callbacków. CAD jest
obserwacją kanału, a nie wymaganą prawem polityką listen-before-talk; za
ewentualną procedurę dostępu odpowiada aplikacja.

Provider SX126x wykonuje pełną kalibrację podczas tworzenia radia oraz zależną
od pasma kalibrację obrazu podczas konfiguracji. Zapamiętuje skalibrowany zakres
częstotliwości, więc kolejne konfiguracje w tym samym zakresie nie wykonują
zbędnej kalibracji obrazu. `hal_lora_radio_calibrate()` jawnie powtarza pełną
kalibrację i kalibrację obrazu dla skonfigurowanej częstotliwości w stanie
standby. Przed konfiguracją modemu lub w czasie innej aktywnej operacji zwraca
`HAL_ESTATE`.
Capabilities SX127x zgłaszają brak jawnej kalibracji, a provider zwraca
`HAL_EUNSUPPORTED` dla tej opcjonalnej operacji.

## Stan, zasilanie i diagnostyka

`hal_lora_radio_get_state()` zwraca stabilny stan publiczny. Jawna maszyna
stanów używa wartości `STANDBY`, `RX`, `TX`, `CAD`, `SLEEP` i `ERROR`.

`hal_lora_radio_sleep()` przełącza wybrane radio do konfiguracji sleep.
`hal_lora_radio_standby()` wybudza radio ze stanu sleep lub error. Aktywne RX/TX
kończy się przez `hal_lora_radio_cancel()`.

`hal_lora_radio_get_diagnostics()` kopiuje liczniki wysłanych i odebranych
pakietów, błędów CRC/nagłówka, timeoutów TX/RX, anulowań, błędów operacji i
magistrali, zdarzeń DIO, wywołań callbacków, odrzuconych pakietów/zdarzeń i
resetów. Liczniki CAD rozróżniają kontrole, kanał zajęty, kanał wolny i timeout.
Liczniki kalibracji rozróżniają kalibrację pełną i obrazu oraz zachowują
zapamiętany zakres częstotliwości. Diagnostyka zwraca również ostatnie RSSI
pakietu, RSSI sygnału, bieżące RSSI, SNR, błąd, timestamp zdarzenia i timestamp
zmiany stanu.

## Time-on-air

`hal_lora_time_on_air()` sprawdza pola modulacji LoRa i pakietu oraz zwraca
zaokrąglony w górę czas trwania pakietu w milisekundach. Limity częstotliwości
i mocy wyjściowej zależą od sprzętu i sprawdza je
`hal_lora_radio_configure()`:

```c
uint32_t airtime_ms = 0;
status = hal_lora_time_on_air(&modem, 32u, &airtime_ms);
```

Używaj airtime przy wybieraniu jawnego timeoutu TX i obliczaniu zachowania duty
cycle zgodnego z przepisami.

## Współbieżność i walidacja

Wywołania runtime są serializowane osobno dla każdego uchwytu. Operacje cyklu
życia (`create` i `destroy`) podlegają ogólnej regule biblioteki: jeden rdzeń i
jeden właściciel. Muszą być wykonywane na rdzeniu, który zarządza GPIO IRQ
providera. Bufory pakietów są kopiowane przed powrotem wywołania start.
Callbacki są wywoływane bez przetrzymywania muteksu uchwytu.

Testy hosta znajdują się w `test_hal_lora_radio_lifecycle`,
`test_hal_lora_radio`, `test_hal_lora_sx127x`, `test_sx126x_adapter`,
`test_sx127x_adapter` i działającym z rzeczywistym schedulerem
`test_lora_freertos_posix`. Budowalny projekt
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) obsługuje
RP2040 i STM32G474. Powtarzalna procedura dla dwóch urządzeń i weryfikator
portu szeregowego są opisane w
[sprzętowej bramce surowej komunikacji LoRa SX1262](03_build_tests.md#sx1262-raw-lora-hardware-gate).
