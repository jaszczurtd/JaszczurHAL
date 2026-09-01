# Natywna aktualizacja OTA

*Dostępne również [po angielsku](../en/OTAWorkflow.md).*

Ten dokument opisuje całą procedurę natywnej aktualizacji OTA w JaszczurHAL:
konfigurację projektu i firmware'u dla danego targetu, artefakty buildu,
pierwszą instalację, integrację z VS Code, komunikację sieciową, reguły zapory
hosta, potwierdzanie nowej wersji po próbnym rozruchu, automatyczny powrót do
poprzedniej wersji, odzyskiwanie oraz granice bezpieczeństwa.

Ogólny model projektu, w którym polecenia są kierowane do właściwego targetu,
opisano w dokumencie [Praca z projektem firmware](FwProjectWorkflow.md).
Publiczne API udokumentowano w sekcji
[`hal_ota`](../api/pl/15_connectivity.md). Implementację referencyjną dla RP
zawiera przykład [`examples/25_ota`](../../examples/25_ota/README.pl.md).

## Macierz wsparcia

| Target | Wgrywany obraz | Model aktywacji | Stan weryfikacji |
|---|---|---|---|
| `rp2040`, `rp2350-arm` | Podpisany kontener `.ota` JaszczurHAL | HAL zamienia zawartość slotu programu z zawartością slotu przejściowego, wykonuje rozruch próbny i w razie potrzeby przywraca poprzedni obraz | Zweryfikowane sprzętowo na Pico W, Pico 2 W oraz Pico+PIM730/RM2 |
| `esp32s3` | Binarny obraz aplikacji ESP-IDF wybrany ze sprawdzonego manifestu buildu | Partycje aplikacji ESP-IDF `two-ota-large`, obraz oczekujący na weryfikację, potwierdzenie albo powrót do poprzedniego obrazu | Implementacja, kompilacja i linkowanie są kompletne; trwa weryfikacja sprzętowa i całego cyklu życia oraz testy negatywne mechanizmów bezpieczeństwa |

Obie platformy korzystają z tego samego protokołu transportowego i publicznych
funkcji zwrotnych. Kontener RP i obraz aplikacji ESP są jednak różnymi
artefaktami i nie można ich wzajemnie konwertować.

<a id="shared-auth2-transport-authentication"></a>
<a id="współdzielone-uwierzytelnianie-transportowe-auth2"></a>

## Uwierzytelnianie transportu AUTH2 wspólne dla obu platform

Jeżeli skonfigurowano niepuste hasło, urządzenie i host przeprowadzają wymianę
`AUTH2`. Każdy błąd kończy ją bez zaakceptowania połączenia:

1. Host wysyła `0 <tcp-port> <image-size> <image-md5>` z jednego połączonego
   gniazda UDP. Urządzenie zapisuje adres IPv4 i port źródłowy tego gniazda.
2. Urządzenie generuje losową wartość nonce o długości 16 bajtów i odpowiada
   `AUTH2 <device-nonce>` na ten zapisany punkt końcowy.
3. Host generuje własną losową wartość nonce o długości 16 bajtów. Obie strony
   tworzą
   dokładny transkrypt ASCII
   `JHOTA-AUTH-2:<command>:<tcp-port>:<image-size>:<image-md5>:<device-nonce>:<client-nonce>`.
   Pola szesnastkowe są normalizowane do małych liter.
4. Obie strony obliczają `MD5(password UTF-8 bytes)`. Wynik zapisują jako
   32-bajtowy ciąg małych znaków szesnastkowych ASCII i używają go jako klucza
   do obliczenia HMAC-SHA256 z przygotowanego transkryptu. Host wysyła
   `201 <client-nonce> <64-hex-character-tag>`.
5. Urządzenie akceptuje tę odpowiedź tylko wtedy, gdy nadeszła z pierwotnego
   adresu IPv4 i portu źródłowego UDP. Odpowiada `OK`, a następnie łączy się z
   tym samym adresem IPv4 na porcie TCP zapisanym w uwierzytelnionym
   transkrypcie. Host przesyła dane tylko wtedy, gdy adres drugiej strony
   połączenia TCP odpowiada adresowi wybranemu przez połączone gniazdo UDP
   hosta.

W datagramach zaproszenia i AUTH2 pola są oddzielone jedną spacją ASCII; nie
wolno dodawać białych znaków na początku ani na końcu. Wiadomość może kończyć się
bezpośrednio po ostatnim polu, jednym LF lub jednym CRLF. Pola liczbowe
używają najkrótszej postaci dziesiętnej. Pola szesnastkowe mogą zawierać
wielkie lub małe litery, lecz przed obliczeniem transkryptu są normalizowane
do małych liter. Osadzone NUL-e, dodatkowe linie, tabulatory,
powtórzone separatory, alternatywne zapisy liczb z wiodącymi zerami,
nieprawidłowe znaczniki i wartości spoza zakresu są odrzucane.

Gdy host ma niepuste hasło, wymaga pełnej wymiany `AUTH2`. Bezpośrednie `OK`,
starsze wyzwanie `AUTH` ani starsza odpowiedź `200` nie powodują przejścia do
słabszego wariantu protokołu. Mechanizm wiązania punktu końcowego odrzuca
również odpowiedź na wyzwanie wysłaną z innego adresu UDP lub portu źródłowego
oraz połączenie zwrotne TCP z innego adresu. Nonce urządzenia pochodzi z
kryptograficznie bezpiecznego generatora liczb losowych dostępnego na danym
targecie; nonce klienta generuje CSPRNG systemu operacyjnego hosta. AUTH2
uwierzytelnia zaproszenie oraz dowód znajomości hasła, lecz nie szyfruje
pakietów służących do wykrywania urządzeń, metadanych ani firmware'u. Wartość MD5 zawarta w
zaproszeniu wiąże je z obrazem dla zachowania zgodności protokołu
transportowego, dlatego nadal jest potrzebna niezależna walidacja obrazu
odpowiednia dla targetu.

Pominięcie `hal_ota_set_password()` lub przekazanie pustego łańcucha sprawia,
że urządzenie akceptuje zaproszenia bez AUTH2. Host zezwala na ten tryb
tylko wtedy, gdy `ota.allowEmptyPassword` jest jawnie ustawione na `true`;
jest to świadome potwierdzenie decyzji operatora, a nie ustawienie urządzenia.
Dowolny uczestnik sieci, który może dotrzeć do usługi UDP OTA, może wówczas
rozpocząć transfer i dostarczyć obraz spełniający pozostałe warunki walidacji
targetu. Używaj tego trybu wyłącznie w odizolowanych sieciach deweloperskich.

## Procedura aktualizacji ESP32-S3

Projekt ESP32-S3 włącza `HAL_ENABLE_OTA` w `hal_project_config.h`.
Wygenerowane domyślne ustawienia ESP-IDF wybierają tabelę partycji
`two-ota-large` i zezwalają na automatyczny powrót do poprzedniej aplikacji.
Pierwszą instalację wykonuje się standardową, sprawdzoną metodą programowania
pamięci flash przez interfejs szeregowy. Przed pierwszą aktualizacją sieciową
urządzenie musi już mieć zgodny bootloader, tabelę partycji i początkową
aplikację.

Użyj standardowego manifestu projektu ESP-IDF wraz ze wspólnymi
ustawieniami punktu końcowego OTA:

```json
{
  "project": "my-device",
  "module": "my_device",
  "toolchain": "esp-idf",
  "target": "esp32s3",
  "board": "waveshare-esp32-s3-zero",
  "buildDir": "${project}/.build/esp32s3",
  "ota": {
    "hostname": "my-device",
    "port": 8266,
    "listenPort": 8266,
    "passwordEnv": "MY_DEVICE_OTA_PASSWORD"
  }
}
```

Gdy połączenie WiFi jest już dostępne, kod urządzenia konfiguruje wspólną
usługę OTA i regularnie wywołuje `hal_ota_handle()`. Próbny rozruch potwierdza
dopiero po pomyślnym zakończeniu wszystkich testów gotowości produktu:

```c
#include <hal/network/ota/hal_ota.h>

hal_ota_set_hostname("my-device");
hal_ota_set_port(8266u);
hal_ota_set_password(APP_OTA_PASSWORD);
if (!hal_ota_begin()) {
    /* Report partition or network setup failure. */
}

/* Repeated service path. */
hal_ota_handle();

/* Run only after the new image passes product startup checks. */
hal_status_t confirm_status = hal_ota_confirm_boot_ex();
```

`Project: Upload (OTA)` lub `jh-vscode upload-ota` uruchamia build produkcyjny
i wymaga `HAL_ENABLE_OTA` w zestawie funkcji po rozwiązaniu zależności.
Narzędzie sprawdza manifest artefaktów ESP-IDF, który można przenosić wraz z
projektem, a rozmiar i SHA-256 pliku BIN aplikacji porównuje z odpowiednim
wpisem obrazu flash. Do urządzenia wysyła same bajty obrazu aplikacji - nie
podpisuje ich ani nie opakowuje w kontener `.ota` używany na RP. Wykrywanie
urządzeń, stały adres `ota.host`, ustawienia `listenPort` i `passwordEnv` oraz
reguły zapory korzystają ze wspólnej procedury po stronie hosta opisanej
poniżej.

Urządzenie zapisuje nieaktywną partycję aplikacji OTA za pomocą `esp_ota_*`,
sprawdza sumę MD5 przesłanego obrazu i jego poprawność za pomocą mechanizmów
ESP-IDF,
wybiera nową partycję rozruchową, po czym uruchamia się ponownie.
`hal_ota_get_boot_info_ex()` przekłada stan partycji i obrazu ESP-IDF na stany
udostępniane przez publiczne API: stabilny, oczekujący, próbny,
przywracanie poprzedniej wersji oraz odzyskiwanie. Gdy uruchomiona próbnie
aplikacja działa prawidłowo,
`hal_ota_confirm_boot_ex()` wywołuje
`esp_ota_mark_app_valid_cancel_rollback()`. Jeśli aplikacja nie zostanie
potwierdzona, bootloader ESP-IDF przywraca poprzednią wersję zgodnie ze
skonfigurowaną polityką.

Używaj unikalnego hasła AUTH2 o dużej entropii we wdrożonych systemach.
AUTH2 i MD5 transferu nie zapewniają współczesnego mechanizmu
kryptograficznego podpisywania obrazów ani poufności. Secure Boot V2,
szyfrowanie flash, zabezpieczenie przed instalacją starszej wersji, chronione
klucze oraz procedury odzyskiwania to osobne zabezpieczenia wymagane w
produkcie. Podczas standardowego wgrywania i testów nie wolno programować
nieodwracalnych bitów eFuse.

Programowanie przez interfejs szeregowy lub JTAG na podstawie kompletnego,
sprawdzonego manifestu pozostaje sposobem odzyskania urządzenia, gdy WiFi,
nowa aplikacja lub metadane OTA są nieużywalne. Obecna implementacja OTA dla
ESP32-S3 została sprawdzona tylko na poziomie kompilacji i linkowania.
Weryfikacji sprzętowej nadal wymagają:
rozruch próbny i jego potwierdzanie, powrót do poprzedniej wersji, przerwane
transfery, nieprawidłowe obrazy, błędy uwierzytelniania i przesyłania oraz
odzyskiwanie.

## Procedura aktualizacji RP

Obsługa natywnego OTA na RP obejmuje oficjalne targety Pico SDK `rp2040` i
`rp2350-arm`. Pełny przebieg aktualizacji przez WiFi sprawdzono na Pico W,
Pico 2 W oraz zwykłym Pico z modułem PIM730/RM2, zarówno w buildach bare-metal,
jak i FreeRTOS.

Procedura obejmuje cztery odrębne etapy:

1. Zbuduj aplikację z `HAL_ENABLE_OTA`. CMake tworzy dwa sloty na firmware,
   obszar kontrolny OTA, niepodpisany kontener `.ota` oraz scalony plik UF2,
   który zawiera program rozruchowy instalujący aktualizację i aplikację.
2. Jednorazowo zainstaluj ten scalony plik UF2 przez BOOTSEL. Niezaprogramowana
   płytka nie może otrzymać aktualizacji przez sieć.
3. Wykryj działającą płytkę przez UDP albo podaj jej adres. Host podpisuje
   kontener skonfigurowanym hasłem bezpośrednio przed wgraniem.
4. Prześlij podpisany kontener do slotu przejściowego. Płytka wykonuje próbny
   rozruch obrazu, a kod aplikacji potwierdza go dopiero po pomyślnym
   zakończeniu testów startowych produktu.

W scalonym pliku UF2 każdy niekońcowy sektor flash, w którym znajduje się choć
część obrazu, zawiera również brakujące strony wypełnione zerami. Rozwiązanie
jest zgodne z obejściem zastosowanym w Pico SDK dla erraty
[RP2040-E14](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf).
Bez takiego dopełnienia nieciągłość danych na granicy między
programem rozruchowym instalującym aktualizację a aplikacją może spowodować,
że BOOTSEL zaprogramuje niepełny obraz.

## Manifest projektu dla RP

Włącz OTA w `.vscode/jaszczurhal.project.json`, wpisz ścieżki do wygenerowanych
artefaktów i zdefiniuj ustawienia wykrywania urządzeń oraz uwierzytelniania po
stronie hosta:

```json
{
  "toolchain": "cmake",
  "target": "rp2040",
  "board": "picow",
  "buildDir": "${project}/.build",
  "cmakeBuildDir": "${buildDir}/cmake",
  "cmake": {
    "sourceDir": "${project}/../libraries/JaszczurHAL/cmake/jh_firmware_project",
    "cache": {
      "JH_PROJECT_DIR": "${project}",
      "JH_MODULE_NAME": "tracker",
      "JH_EXTRA_DEFINES": "HAL_ENABLE_OTA",
      "JH_OTA_GENERATION": 7,
      "JH_OTA_VERSION": "1.4.0"
    }
  },
  "artifacts": {
    "elf": "${buildDir}/firmware.elf",
    "uf2": "${buildDir}/firmware.uf2",
    "ota": "${buildDir}/firmware.ota",
    "compileCommands": "${buildDir}/compile_commands_patched.json"
  },
  "ota": {
    "hostname": "tracker-office",
    "port": 8266,
    "listenPort": 8266,
    "passwordEnv": "TRACKER_OTA_PASSWORD"
  }
}
```

`JH_EXTRA_DEFINES` to lista CMake rozdzielona średnikami. Zachowaj inne
definicje projektu przy dodawaniu OTA, na przykład
`"HAL_ENABLE_OTA;HAL_ENABLE_FREERTOS"`. `HAL_ENABLE_OTA` automatycznie
włącza wymagane moduły WiFi, UDP, TCP, kryptografii i CRC.

Metadane buildu mają następujący format:

| Ustawienie | Znaczenie |
|---|---|
| `JH_OTA_GENERATION` | 32-bitowa liczba bez znaku określająca generację obrazu i przechowywana w kontenerze. Zwiększaj ją zgodnie z polityką wydań projektu. To metadane, a nie wymuszany mechanizm zapobiegający instalacji starszej wersji. Wartość domyślna to `1`. |
| `JH_OTA_VERSION` | Wersja obrazu zapisana w czytelnej postaci. Musi być krótsza niż 32 bajty UTF-8. Wartość domyślna to `dev`. |
| `artifacts.ota` | Zalecane dokładne wskazanie ścieżki do niepodpisanego kontenera. Bez tej wartości narzędzie akceptuje tylko jeden, jednoznacznie wskazany plik `.ota` poniżej `buildDir`. |
| `artifacts.uf2` | Scalony obraz programu rozruchowego instalującego aktualizację i aplikacji, używany do pierwszej instalacji i odzyskiwania przez USB. |

Ustawienia w obiekcie `ota` sterują działaniem narzędzia po stronie hosta:

| Ustawienie | Znaczenie |
|---|---|
| `hostname` | Nazwa urządzenia używana do filtrowania listy wykrytych urządzeń. Gdy korzystasz z wykrywania, musi odpowiadać wartości przekazanej w firmwarze do `hal_ota_set_hostname()`. |
| `port` | Port UDP urządzenia używany do wykrywania, zaproszeń i uwierzytelniania. Musi odpowiadać wartości przekazanej do `hal_ota_set_port()`. Wartość domyślna to `8266`. Nie jest to port transferu danych TCP. |
| `listenPort` | Ogłaszany urządzeniu port hosta dla połączenia zwrotnego TCP. Wartość domyślna to `8266`, zgodnie z trwałą regułą przygotowaną przez `runmefirst.sh`. Ustaw `0` tylko wtedy, gdy świadomie wybierasz port przydzielany dynamicznie i odpowiednio skonfigurowaną zaporę. |
| `passwordEnv` | Nazwa zmiennej środowiskowej hosta zawierającej hasło OTA. Ma pierwszeństwo przed `ota.password`. |
| `password` | Hasło wpisane bezpośrednio w manifeście, przeznaczone wyłącznie do prac deweloperskich. Nie używaj go w manifeście produktu przechowywanym w repozytorium. |
| `allowEmptyPassword` | Jawna wartość `true` zezwala na puste hasło. Domyślnie host takie hasła odrzuca. Nie włączaj tej opcji na wdrożonych urządzeniach. |
| `broadcast` | Adres docelowy pakietów służących do wykrywania urządzeń. Wartość domyślna to `255.255.255.255`. Na hostach z wieloma interfejsami często pewniej działa adres rozgłoszeniowy konkretnej podsieci, na przykład `192.168.2.255`, albo bezpośredni adres urządzenia. |
| `host` | Stały adres IPv4 urządzenia lub nazwa hosta, którą można rozwiązać. Podczas wgrywania pozwala pominąć wykrywanie rozgłoszeniowe, a polecenie wykrywania kieruje zapytanie bezpośrednio do urządzenia. Ustawienie nadaje się do automatyzacji i sieci z routingiem. Opcja `--host` w wierszu poleceń zastępuje tę wartość przy pojedynczym wywołaniu. |

Każdemu urządzeniu, które może być zasilane równocześnie z innymi, nadaj
unikalną nazwę hosta. Wybór urządzenia uwzględnia również target aktywny w
bieżącej konfiguracji. Jeśli wykryto więcej niż jedno pasujące urządzenie,
wybierz je interaktywnie albo ustaw stały adres w `ota.host`; w procesie
automatycznym wybór musi być jednoznaczny.

## Sekret i konfiguracja po stronie urządzenia RP

To samo hasło musi być dostępne po obu stronach procedury:

- firmware przekazuje je do `hal_ota_set_password()`;
- host odczytuje identyczny łańcuch z `ota.passwordEnv` i używa go do
  uwierzytelniania transportowego i podpisywania kontenera.

Nie umieszczaj hasła produktu w `app.c`, manifeście ani zadaniu przechowywanym
w repozytorium, ani w ustawieniach VS Code. W prostym środowisku deweloperskim
użyj szablonu śledzonego w repozytorium oraz lokalnego nagłówka wykluczonego z
kontroli wersji:

```text
tracker/
  app.c
  hal_project_config.h
  ota_secrets.example.h
  ota_secrets.h
  .gitignore
```

```c
/* ota_secrets.example.h - tracked template */
#pragma once
#define APP_WIFI_SSID "replace-with-wifi-ssid"
#define APP_WIFI_PASSWORD "replace-with-wifi-password"
#define APP_OTA_PASSWORD "replace-with-a-unique-high-entropy-secret"
```

```gitignore
# .gitignore
/ota_secrets.h
```

Skopiuj szablon do `ota_secrets.h`, lokalnie zastąp przykładowe wartości i
dołącz ten plik do firmware'u. W produktach, które otrzymują konfigurację
podczas wdrażania, hasło może zamiast tego pochodzić z bezpiecznego magazynu
właściwego dla produktu. Musi zostać odczytane przed wywołaniem
`hal_ota_begin()`. JaszczurHAL nie zapewnia chronionego magazynu kluczy na
RP2040/RP2350; napastnik z dostatecznym dostępem fizycznym może odczytać sekret
zapisany w firmware.

Wyeksportuj to samo hasło w powłoce, z której uruchamiasz `jh-vscode`:

```bash
export TRACKER_OTA_PASSWORD='the-same-value-used-by-firmware'
```

Zadania VS Code dziedziczą środowisko procesu VS Code. Nie widzą zmiennych
wyeksportowanych później w osobno uruchomionym terminalu zintegrowanym. Po
zmianie zmiennej na Linuksie zamknij wszystkie procesy VS Code, a następnie
uruchom projekt z odpowiednio skonfigurowanej powłoki:

```bash
export TRACKER_OTA_PASSWORD='the-same-value-used-by-firmware'
code .
```

Wartość `passwordEnv` to tylko nazwa zmiennej; nie wpisuj
`"${TRACKER_OTA_PASSWORD}"` w manifeście.

## Integracja firmware'u RP

Przed uruchomieniem usługi OTA skonfiguruj nazwę hosta, port UDP, hasło i
opcjonalne funkcje zwrotne. Uruchom usługę dopiero po zestawieniu połączenia
sieciowego i często wywołuj `hal_ota_handle()`. Rozruch próbny potwierdź dopiero
po pomyślnym przejściu wszystkich testów gotowości właściwych dla produktu.

Poniższy szkielet pokazuje pełny sposób sterowania usługą przez aplikację:

```c
#include <hal/core/hal_app.h>
#include <hal/network/ota/hal_ota.h>
#include <hal/core/hal_status.h>
#include <hal/system/hal_system.h>
#include <hal/network/hal_wifi.h>

#include "ota_secrets.h"

static const char *OTA_HOSTNAME = "tracker-office";
static const uint16_t OTA_PORT = 8266u;

static bool ota_configured;
static bool ota_started;
static bool boot_confirmed;
static uint32_t last_wifi_attempt_ms;

static bool application_startup_checks_passed(void) {
  /* Replace with real checks for required sensors, storage, and services. */
  return true;
}

static void ota_error(hal_ota_error_t error, void *user) {
  (void)error;
  (void)user;
}

static void connect_wifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }
  const uint32_t now = hal_millis();
  if (now - last_wifi_attempt_ms < 5000u) {
    return;
  }
  last_wifi_attempt_ms = now;
  (void)hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  (void)hal_wifi_set_hostname(OTA_HOSTNAME);
  (void)hal_wifi_begin_station(APP_WIFI_SSID, APP_WIFI_PASSWORD, true);
}

void app_start(void) {
  ota_configured =
      hal_ota_set_hostname(OTA_HOSTNAME) &&
      hal_ota_set_port(OTA_PORT) &&
      hal_ota_set_password(APP_OTA_PASSWORD) &&
      hal_ota_on_error(ota_error, NULL);
}

void app_task0(void) {
  connect_wifi();

  if (ota_configured && hal_wifi_is_connected() && !ota_started) {
    ota_started = hal_ota_begin();
  }

  if (ota_started) {
    hal_ota_handle();
    if (!boot_confirmed && application_startup_checks_passed()) {
      boot_confirmed = hal_ota_confirm_boot_ex() == HAL_OK;
    }
  }

  hal_delay_ms(1u);
}
```

Bezwarunkowe `true` w `application_startup_checks_passed()` służy tylko jako
przykład. Gotowy produkt powinien sprawdzać każdy warunek wymagany
do uznania nowego obrazu za bezpieczny: zgodność konfiguracji, zamontowanie
wymaganej pamięci masowej, obecność wymaganego sprzętu, działanie usług
sieciowych i wynik
autotestów aplikacji. Zbyt wczesne potwierdzenie odbiera możliwość
automatycznego przywrócenia poprzedniego obrazu po późniejszej awarii rozruchu.

Dodatkowe reguły API:

- `hal_ota_set_port()` odrzuca port zero i nie może zmienić portu po
  uruchomieniu usługi.
- Nazwa hosta musi być niepusta. Jeśli jej nie podano, domyślnie używana jest
  nazwa targetu HAL.
- Pominięcie hasła urządzenia lub ustawienie go na pusty łańcuch pomija
  AUTH2. Host odrzuca ten tryb, chyba że `allowEmptyPassword` jest jawnie
  ustawione. Zawsze ustawiaj niepusty sekret produktu.
- `hal_ota_handle()` obsługuje sieć, wykrywanie urządzeń, uwierzytelnianie i
  transfer oraz wywołuje zarejestrowane funkcje zwrotne. Nie przestawaj go
  wywoływać, dopóki OTA jest włączone.
- Jeśli nie uda się przydzielić mutexu używanego przez backend, usługa
  pozostaje zatrzymana:
  funkcje zwracające wartość logiczną zwracają `false`, funkcje zwracające
  status - `HAL_ENOMEM`, a `hal_ota_handle()` niczego nie robi.
- Urządzenie restartuje się automatycznie po przyjęciu kompletnego obrazu i
  sprawdzeniu jego poprawności.
- `hal_ota_get_boot_info_ex()` zwraca stan stabilny, próbny, przywracania
  poprzedniej wersji lub odzyskiwania wraz z generacją, wersją, liczbą prób i
  ich limitem.
- `HAL_RP_OTA_MAX_BOOT_ATTEMPTS` domyślnie wynosi `3` i akceptuje wartości
  od 1 do 255. Nadpisuj go przez `JH_EXTRA_DEFINES` lub
  `hal_project_config.h` tylko wtedy, gdy produkt ma jawnie określoną politykę
  rozruchu.

W aplikacji FreeRTOS uruchamiaj usługę z jednego zadania. Przydziel mu stos
wystarczający do inicjalizacji CYW43 i obsługi OTA. Stanowisko do sprzętowych
testów regresyjnych używa 2048 słów stosu FreeRTOS, czyli 8 KiB na RP:

```c
/* hal_project_config.h */
#pragma once

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 2048u
#endif
```

Zmierz rzeczywiste maksymalne wykorzystanie stosu w gotowym produkcie zamiast
zakładać, że ta wartość wystarczy w każdym zastosowaniu.

## Artefakty buildu RP i pierwsza instalacja

Przed pierwszym buildem sprawdź target wybrany po przetworzeniu konfiguracji,
płytkę, ścieżki i ustawienia OTA:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

Zbuduj z katalogu projektu firmware'u:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  build --project "$PWD"
```

Build natywny RP z włączonym OTA tworzy:

| Artefakt | Przeznaczenie |
|---|---|
| `firmware.elf` | Symbole do debugowania i analizy mapy pamięci. |
| `firmware.bin` | Binarny obraz aplikacji zlinkowany pod adres slotu programu. |
| `firmware.ota` | Niepodpisany i nieszyfrowany kontener OTA. Zawiera identyfikator targetu, przesunięcie ładowania, generację, wersję, SHA-256 danych obrazu oraz CRC nagłówka, ale nie hasło. |
| `firmware.uf2` | Scalony obraz programu rozruchowego instalującego aktualizację i aplikacji, przeznaczony do pierwszej instalacji lub odzyskiwania przez USB. |
| `firmware.signed.ota` | Artefakt tworzony podczas wgrywania przez `upload-ota`; nadal nie jest szyfrowany i zwykle znajduje się poniżej `buildDir`. |

Kontener v1 zaczyna się od 160-bajtowego nagłówka zapisanego w porządku
little-endian:

| Zakres bajtów | Pole |
|---|---|
| `0..7` | Sygnatura `JHOTA1\r\n` |
| `8..9` | Wersja nagłówka, obecnie `1` |
| `10..11` | Rozmiar nagłówka, obecnie `160` |
| `12..13` | ID targetu: `1` RP2040, `2` RP2350 Arm, `3` RP2350 RISC-V |
| `14..15` | Zarezerwowane |
| `16..19` | Przesunięcie slotu programu w pamięci flash |
| `20..23` | Rozmiar danych obrazu |
| `24..27` | Monotoniczna generacja obrazu |
| `28..31` | Flagi |
| `32..63` | SHA-256 danych obrazu |
| `64..95` | Wersja UTF-8, wypełniona NUL-ami; zakodowana wartość musi być krótsza niż 32 bajty |
| `96..127` | HMAC-SHA256 |
| `128..155` | Zarezerwowane |
| `156..159` | CRC32 bajtów nagłówka `0..155` |

Po utworzeniu kontenera pole HMAC pozostaje wyzerowane. Podczas wgrywania
narzędzie sprawdza skrót danych obrazu, oblicza MD5 bajtów hasła w UTF-8 i
zapisuje wynik szesnastkowo małymi literami ASCII. Tych 32 bajtów ASCII używa
jako klucza HMAC-SHA256 dla bajtów nagłówka `0..95`. Wynik HMAC zapisuje w
bajtach `96..127`, po czym ponownie oblicza CRC32.
Ten sposób wyprowadzania klucza zachowuje zgodność z protokołem transportowym
OTA, ale nie utrudnia odgadywania hasła. Przed oznaczeniem slotu przejściowego
jako oczekującego urządzenie sprawdza zgodność targetu i układu pamięci,
dozwolone zakresy, CRC nagłówka, HMAC oraz skrót danych obrazu.

OTA rezerwuje obszar rozruchowy o rozmiarze 16 KiB, dwa równe sloty - programu
i przejściowy - oraz cztery sektory kontrolne po 4 KiB. Dopiero za nimi może
znajdować się końcowy obszar LittleFS/EEPROM. Dostępna przestrzeń aplikacji
jest więc mniejsza niż w buildzie bez OTA. CMake wyznacza układ pamięci dla
wybranego rozmiaru flash i zgłasza błąd, jeśli obszary się nakładają albo
aplikacja się nie mieści.

Dla niezaprogramowanej płytki:

1. Przytrzymaj BOOTSEL podczas podłączania lub resetowania płytki.
2. Upewnij się, że w systemie widoczny jest tylko właściwy dysk BOOTSEL RP.
3. Uruchom:

   ```bash
   ../libraries/JaszczurHAL/vscode/entry/jh-vscode \
     upload-uf2 --project "$PWD"
   ```

Standardowa akcja `upload` również zapisuje UF2 przez USB, lecz reset
firmware'u sygnałem CDC 1200 bps może zadziałać dopiero po jego pierwszej
instalacji. `upload` nie jest poleceniem OTA. Ręczne użycie BOOTSEL pozostaje
metodą odzyskiwania, gdy nie można uruchomić aplikacji, WiFi lub usługi OTA.

## Wykrywanie urządzeń i wgrywanie OTA na RP

Poczekaj, aż firmware połączy się z WiFi, a `hal_ota_begin()` zakończy się
powodzeniem. Następnie wyświetl listę urządzeń:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  ota-discover --project "$PWD"
```

Opcja `--json` zwraca wynik wykrywania w formacie przeznaczonym do
automatycznego przetwarzania. Odpowiedź zawiera nazwę hosta i adres urządzenia,
target, port UDP, rozmiar slotu, aktywną generację oraz tryb rozruchu.

Wgraj interaktywnie:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" --interactive
```

Jeśli znasz adres urządzenia, możesz pominąć wykrywanie:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" --host 192.168.2.200
```

`upload-ota` zawsze najpierw wykonuje build. Następnie znajduje niepodpisany
artefakt `.ota`, odczytuje hasło, tworzy `firmware.signed.ota`, uwierzytelnia
zaproszenie, przesyła obraz, czeka na jego zaakceptowanie przez urządzenie i
informuje o ponownym uruchomieniu urządzenia. Automatyczny wybór następuje
tylko wtedy, gdy dokładnie jedno wykryte urządzenie pasuje do aktywnego targetu
i skonfigurowanej nazwy hosta.

Jeśli projekt zawiera konfiguracje dla kilku kombinacji targetu i płytki,
najpierw wybierz właściwy profil albo jawnie podaj parametry, które mają
zastąpić konfigurację:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" \
  --target rp2350-arm --board pico2w --host 192.168.2.200
```

`--variant` ma zastosowanie tylko wtedy, gdy manifest deklaruje dany wariant.
Na przykład wariant `freertos` wybiera się za pomocą `--variant freertos`.

## Zadania VS Code i skróty klawiszowe dla RP

Wygenerowane projekty zawierają następujące zadania:

| Zadanie | Przeznaczenie |
|---|---|
| `Project: Build` | Zbuduj wszystkie artefakty wybranego targetu, w tym `.ota` i scalony UF2. |
| `Project: Upload (UF2 / BOOTSEL)` | Pierwsza instalacja lub odzyskiwanie przez USB. |
| `Project: Discover OTA devices` | Wykryj i wyświetl pasujące urządzenia OTA. |
| `Project: Upload (OTA)` | Zbuduj, wybierz urządzenie interaktywnie, podpisz i wgraj przez sieć. |
| `Project: Config Dump` | Sprawdź manifest po uwzględnieniu całej konfiguracji oraz lokalny wybór targetu i płytki. |

W projektach przeniesionych na bieżącą wersję należy skopiować aktualne
definicje zadań z
[`vscode/examples/tasks.json`](../../vscode/examples/tasks.json). Zadania
wywołują `${config:jaszczurhal.vscodeEntry}`, dlatego ustawienie to w
`.vscode/settings.json` musi wskazywać lokalny katalog źródłowy JaszczurHAL
używany przez projekt.

Zalecane skróty klawiszowe są następujące:

| Skrót | Zadanie |
|---|---|
| `Ctrl+Shift+1` | `Project: Build` |
| `Ctrl+Shift+4` | `Project: Upload (UF2 / BOOTSEL)` |
| `Ctrl+Shift+8` | `Project: Upload (OTA)` |
| `Ctrl+Shift+9` | `Project: Config Dump` |
| `Ctrl+Shift+Alt+3` | `Project: Discover OTA devices` |

Pliki `.vscode/keybindings.reference.json` w projekcie pełnią wyłącznie funkcję
dokumentacyjną. VS Code nie wczytuje skrótów klawiszowych zapisanych lokalnie
w repozytorium. Dodaj odpowiednie wpisy do właściwego pliku skrótów
użytkownika za pomocą polecenia **Preferences: Open Keyboard Shortcuts
(JSON)**:

```json
[
  {
    "key": "ctrl+shift+8",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Upload (OTA)"
  },
  {
    "key": "ctrl+shift+alt+3",
    "command": "workbench.action.tasks.runTask",
    "args": "Project: Discover OTA devices"
  }
]
```

Na Linuksie plik użytkownika znajduje się zwykle w
`~/.config/Code/User/keybindings.json`. Po zmianie przypisań przeładuj okno.
Nie myl skrótów z katalogu głównego repozytorium, które działają na samej
bibliotece JaszczurHAL, ze skrótami projektu firmware'u.

## Komunikacja sieciowa i zapora hosta dla RP

W kanale danych OTA to urządzenie zestawia połączenie z hostem:

1. Host wysyła przez UDP pakiety służące do wykrywania urządzeń, zaproszenia i
   uwierzytelniania do skonfigurowanego portu OTA urządzenia, zwykle `8266`.
2. Host otwiera gniazdo nasłuchujące TCP. `ota.listenPort` wybiera jego port;
   wartość domyślna to `8266`, natomiast jawna wartość `0` zleca systemowi
   operacyjnemu dynamiczne przydzielenie portu.
3. Zaproszenie informuje urządzenie o tym porcie TCP.
4. Urządzenie inicjuje nowe połączenie TCP do hosta i odbiera obraz we
   fragmentach z potwierdzeniem odbioru.

Przy `"listenPort": 8266` pakiet SYN z urządzenia jest kierowany na port TCP
8266 hosta, więc wystarczy reguła zapory obejmująca dokładnie ten port
połączenia zwrotnego. Przy `"listenPort": 0` Linux zwykle wybiera port z
zakresu `/proc/sys/net/ipv4/ip_local_port_range`; reguły zapory muszą obejmować
wybrany w ten sposób port. Punkt dostępowy lub sieć z routingiem również muszą
zezwalać na ruch z urządzenia do hosta. W sieci OTA wyłącz izolację klientów
bezprzewodowych.

`runmefirst.sh` wykrywa sieć RFC1918 połączoną z domyślnym interfejsem IPv4 i
sprawdza obecność trwałej reguły dla połączenia zwrotnego TCP/8266. W systemie
Windows uruchom ten sam skrypt pomocniczy w Pythonie, korzystając z zarządzanego
środowiska. Jeśli reguły brakuje, przed prośbą o potwierdzenie skrypt podaje
dokładny interfejs, podsieć źródłową, port i mechanizm utrwalania reguł. Podaje
również, czy będzie potrzebna instalacja pakietu lub podniesienie uprawnień.
Odmowa pozostawia zaporę bez zmian i nie blokuje dalszej konfiguracji. Host z
pustym łańcuchem `INPUT` i polityką `ACCEPT` już zezwala na połączenie zwrotne,
dlatego konfiguracja kończy się powodzeniem bez instalowania narzędzi do
utrwalania reguł.

Po uzyskaniu potwierdzenia skrypt korzysta z aktywnego menedżera zapory:

- w aktywnym UFW dodaje trwałą regułę ograniczoną do interfejsu i podsieci;
- w aktywnym firewalld dodaje pasujące reguły typu rich rule w konfiguracji
  bieżącej i trwałej;
- na hoście `iptables-nft`/`iptables` dodaje wczesną regułę `INPUT` i utrwala
  ją za pomocą `netfilter-persistent`; gdy dostępny jest systemd, włącza przy
  starcie systemu usługę odtwarzającą reguły;
- instaluje `iptables-persistent` przez `apt` tylko wtedy, gdy wariant oparty
  na iptables wymaga trwałości, a żadne obsługiwane narzędzie do utrwalania
  reguł nie jest dostępne;
- w Zaporze Windows Defender tworzy nazwaną regułę ruchu przychodzącego,
  ograniczoną do profilu `Private`, wybranego aliasu interfejsu, podsieci
  źródłowej RFC1918, protokołu TCP i portu połączenia zwrotnego. Nie zmienia
  profilu sieci z `Public` na `Private`.

Skrypt konfiguracyjny nigdy nie uruchamia wcześniej nieaktywnego UFW ani
firewalld.
W wariancie opartym na iptables zapisuje cały aktywny zestaw reguł IPv4 do
`/etc/iptables/rules.v4`, bez reguł IPv6; informuje o tym w prośbie o
potwierdzenie. Uruchom skrypt ponownie po zmianie sieci LAN, interfejsu lub
portu połączenia zwrotnego:

```bash
python3 scripts/configure_ota_firewall.py
python3 scripts/configure_ota_firewall.py --check
python3 scripts/configure_ota_firewall.py --dry-run
python3 scripts/configure_ota_firewall.py \
  --interface enp7s0 --network 192.168.2.0/24
```

W systemie Windows użyj interpretera Pythona z zarządzanego środowiska oraz
aliasu interfejsu wyświetlanego przez `Get-NetConnectionProfile`:

```powershell
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --dry-run `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --check `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
```

Jeśli sprawdzane połączenie ma profil `Public`, wybierz zaufaną sieć LAN i
przed dodaniem reguły świadomie zmień profil połączenia w ustawieniach Windows.
Zmiany wprowadź dopiero po przejrzeniu wyniku `--dry-run`, z poziomu
PowerShella uruchomionego z uprawnieniami administratora. Skrypt informuje o
wymaganych uprawnieniach, lecz sam nie wywołuje UAC.

Skrypt akceptuje wyłącznie źródłowe podsieci IPv4 z zakresów RFC1918. Opcja
`--yes` pozwala dodać regułę bez interakcji, jeśli wcześniej jawnie wskazano
interfejs i sieć. Skrypt nie otworzy portu, gdy nasłuchuje na nim już inny
proces. Narzędzie do wgrywania tworzy gniazdo nasłuchujące TCP tylko na czas
transferu; trwała reguła zapory nie uruchamia żadnej usługi w tle.

Określ trasę, interfejs używany przez połączenie zwrotne oraz aktywną
implementację zapory:

```bash
jh_ota_device_ip=192.168.2.200
ip route get "$jh_ota_device_ip"
sudo nft list ruleset
sudo iptables-save
```

Jako interfejs wejściowy podaj wartość `dev` wyświetloną przez `ip route get`.
Adres hosta w wyniku tego polecenia jest adresem docelowym połączenia
zwrotnego. Przy stałym porcie sprawdź, czy proces wgrywania rzeczywiście
nasłuchuje, i przechwyć przebieg nawiązywania połączenia:

```bash
ss -ltn 'sport = :8266'
sudo tcpdump -ni enp7s0 \
  'host 192.168.2.200 and (udp port 8266 or tcp port 8266)'
```

Jeśli w przechwyconym ruchu widać powtarzające się pakiety SYN urządzenia bez
odpowiedzi SYN-ACK, próba połączenia zwrotnego dotarła do hosta. Przyczyny
należy wtedy szukać w zaporze hosta albo w procesie nasłuchującym. Jeśli po
pomyślnym uwierzytelnieniu UDP nie pojawia się żaden SYN, sprawdź adres
połączenia zwrotnego podany urządzeniu, trasę oraz izolację klientów w punkcie
dostępowym.

Na hoście nftables zarządzanym przez warstwę zgodności `iptables-nft` skrypt
instaluje równoważną trwałą regułę. Wyłącznie na potrzeby ręcznej diagnostyki
można wstawić tymczasową regułę ograniczoną do źródłowego adresu urządzenia.
Najpierw potwierdź aktywny łańcuch `INPUT` za pomocą `nft list ruleset` lub
`iptables-save`:

```bash
sudo /usr/sbin/iptables-nft -I INPUT 1 \
  -i enp7s0 \
  -s 192.168.2.200/32 \
  -d 192.168.2.180/32 \
  -p tcp --dport 8266 \
  -m conntrack --ctstate NEW \
  -m comment --comment 'JaszczurHAL OTA callback' \
  -j ACCEPT
```

Zastąp interfejs i oba adresy wartościami z wyniku `ip route get`. Sprawdź
licznik za pomocą:

```bash
sudo /usr/sbin/iptables-nft \
  -L INPUT -n -v --line-numbers
```

Usuń tymczasową regułę, podając to samo pełne dopasowanie oraz `-D` zamiast
`-I ... 1`. Bezpośrednie zmiany w łańcuchu warstwy zgodności mogą zniknąć po
przeładowaniu zapory lub ponownym uruchomieniu systemu. Do zwykłej, trwałej
konfiguracji używaj skryptu pomocniczego.

Gdy `listenPort` wynosi zero, za pomocą `ss -ltnp` sprawdź port wybranego
gniazda nasłuchującego, a zakres portów hosta odczytaj poleceniem:

```bash
cat /proc/sys/net/ipv4/ip_local_port_range
```

Standardowa zapora stanowa zezwala na odpowiedź UDP na wychodzący z hosta
pakiet wykrywania lub zaproszenia. Restrykcyjne reguły ruchu wychodzącego
muszą dodatkowo zezwalać na UDP z hosta do urządzenia przez skonfigurowany
port OTA oraz na ruch odpowiedzi. Samo `jh-vscode` nie wymaga `sudo`.

Jeśli wykrywanie rozgłoszeniowe jest zablokowane, ale znasz adres urządzenia,
użyj `ota.host`, `--host` albo wpisz bezpośredni adres urządzenia w
`ota.broadcast`. Pozwala to pominąć jedynie pakiety rozgłoszeniowe; zapora
nadal musi zezwalać na połączenie zwrotne TCP.

## Potwierdzanie rozruchu próbnego, przywracanie obrazu i odzyskiwanie na RP

Po udanym transferze program rozruchowy instalujący aktualizację zamienia
miejscami zawartość slotu przejściowego i slotu programu, po czym uruchamia
nowy obraz w stanie `HAL_OTA_BOOT_TRIAL`. Każdy niepotwierdzony rozruch
zwiększa licznik prób. Po osiągnięciu zapisanego limitu program ponownie
zamienia sloty, przywracając poprzedni obraz jako stabilny.

Wywołuj `hal_ota_confirm_boot_ex()` dopiero wtedy, gdy nowy obraz spełni
wszystkie kryteria poprawnego uruchomienia. Wywołanie tej funkcji dla obrazu,
który jest już stabilny, jest bezpieczne. W diagnostyce korzystaj z
`hal_ota_get_boot_info_ex()`, aby w dziennikach można było odróżnić zwykły
stabilny rozruch od rozruchu próbnego, przywrócenia poprzedniej wersji i
odzyskiwania.

Zachowaj możliwość odzyskania urządzenia przez USB:

- BOOTSEL wraz ze scalonym `firmware.uf2` pozwala ponownie zainstalować
  program rozruchowy instalujący aktualizację i aplikację, gdy awaria
  uniemożliwia uruchomienie sieci.
- Zmiana rozmiaru flash, układu obszarów OTA lub pamięci masowej albo targetu może
  pozostawić sektory z niezgodnymi informacjami o stanie OTA. Ponownie
  przygotuj urządzenie lub wymaż jego pamięć dopiero po zabezpieczeniu
  wszystkich potrzebnych danych LittleFS/EEPROM.
- Polecenia kasowania sektorów kontrolnych właściwe dla poszczególnych
  targetów, podane w opisie
  [sprzętowego testu natywnego OTA na RP](../api/pl/03_build_tests.md#sprzętowy-test-natywnego-ota-na-rp),
  odnoszą się do dokładnego układu pamięci tego stanowiska testowego. Nie są to
  uniwersalne zakresy kasowania dla produktu.
- Fizyczny dostęp do BOOTSEL pozostaje poza granicą zaufania OTA.

## Granica bezpieczeństwa RP

W podpisanym kontenerze nagłówek z numerem wersji jest uwierzytelniany za pomocą
HMAC-SHA256. Przed aktywacją sprawdzane są również SHA-256 danych obrazu i CRC
nagłówka. To samo hasło uwierzytelnia
[wspólny mechanizm AUTH2 warstwy transportowej](#współdzielone-uwierzytelnianie-transportowe-auth2).
Zapewnia to uwierzytelnianie i integralność, ale nie poufność. HMAC-SHA256
obrazu RP i SHA-256 danych obrazu są sprawdzane niezależnie przed
zaakceptowaniem slotu przejściowego; AUTH2 nie zastępuje żadnego z tych
mechanizmów.

- firmware i oba artefakty `.ota` nie są szyfrowane;
- każdy, kto zna hasło, może stworzyć obraz, który urządzenie zaakceptuje;
- sama generacja obrazu nie zapobiega instalacji starszej wersji, dlatego
  poprawnie podpisany starszy obraz można wgrać ponownie, chyba że produkt
  narzuca własną politykę;
- używaj unikalnego hasła o dużej entropii dla każdego produktu lub grupy
  urządzeń;
- wykonuj OTA wyłącznie w zaufanym segmencie sieci albo dodaj właściwą dla
  produktu warstwę szyfrowanego transportu lub VPN.

Aktualny opis właściwości bezpieczeństwa znajduje się w dokumencie
[Bezpieczeństwo łańcucha dostaw](security_supply_chain.md#native-ota-security-boundary).

## Lista kontrolna rozwiązywania problemów RP

Jeśli wykrywanie urządzeń lub wgrywanie zawiedzie, sprawdź kolejno:

1. `config-dump` wskazuje wybrany natywny target RP, płytkę WiFi, nazwę
   hosta, port UDP, nazwę zmiennej środowiskowej z hasłem oraz ścieżki
   artefaktów.
2. Firmware zbudowano z `HAL_ENABLE_OTA`; `firmware.ota` i scalony
   `firmware.uf2` znajdują się poniżej wyznaczonego `buildDir`.
   Na RP2040 każdy sektor zawierający dane przed ostatnią stroną UF2 musi
   mieć wszystkie szesnaście stron po 256 bajtów; wymusza to
   `test_rp_ota_artifacts`.
3. Urządzenie połączyło się z WiFi, `hal_ota_begin()` zwróciło true, a
   `hal_ota_handle()` nadal działa.
4. Firmware i host używają tego samego portu, nazwy hosta i dokładnie
   tych samych bajtów hasła. Jeśli narzędzie zgłasza brak `passwordEnv`,
   uruchom ponownie VS Code z odpowiednio skonfigurowanego środowiska.
5. Aktywny target jest zgodny z wykrytym urządzeniem. Przy kilku pasujących
   urządzeniach użyj `--interactive`, a przy znanym adresie - `--host`.
6. Pakiety rozgłoszeniowe docierają do właściwego interfejsu. Na hostach z
   wieloma interfejsami oraz w sieciach z routingiem preferuj adres
   rozgłoszeniowy konkretnej podsieci albo bezpośredni adres urządzenia.
7. Punkt dostępowy zezwala na ruch z urządzenia do hosta, a licznik reguły
   zapory hosta rośnie, gdy płytka rozpoczyna połączenie zwrotne TCP. Jeśli
   interfejs lub LAN się zmieniły, uruchom ponownie
   `scripts/configure_ota_firewall.py`.
8. Powtarzające się błędy uwierzytelniania zwykle oznaczają niezgodność
   hasła. Przekroczenie limitu czasu oczekiwania na zaakceptowanie połączenia
   TCP po udanym uwierzytelnieniu UDP zwykle wskazuje błędną regułę zapory dla
   połączenia zwrotnego albo nieprawidłową trasę.
9. Natychmiastowe przywrócenie poprzedniej wersji oznacza, że aplikacja nie
   potwierdziła próbnego rozruchu przed osiągnięciem limitu albo jej testy
   gotowości nigdy nie zakończyły się powodzeniem.
10. Płytka przenoszona między niezgodnymi obrazami różniącymi się targetem,
    runtime'em lub układem pamięci może wymagać ponownego przygotowania przez
    USB zgodnie z kontrolowaną procedurą albo wyczyszczenia metadanych OTA.
