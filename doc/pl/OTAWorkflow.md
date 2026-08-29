# Natywny workflow OTA

*Dostępne również [po angielsku](../en/OTAWorkflow.md).*

Ten dokument to kompletna specyfikacja operacyjna natywnego OTA JaszczurHAL:
konfiguracja projektu i firmware'u specyficzna dla danego targetu, artefakty
buildu, pierwsza instalacja, integracja z VS Code, przepływ sieciowy,
reguły zapory hosta, potwierdzanie próbne, wycofanie (rollback), odzyskiwanie
oraz granice bezpieczeństwa.

Ogólny model projektu oparty na dispatcherze pozostaje opisany w
[Workflow projektu firmware](FwProjectWorkflow.md). Publiczne API jest
udokumentowane pod [`hal_ota`](../api/pl/15_connectivity.md).
Implementacja referencyjna RP to
[`examples/25_ota`](../../examples/25_ota/README.md).

## Macierz wsparcia

| Target | Wgrywany obraz | Model aktywacji | Stan weryfikacji |
|---|---|---|---|
| `rp2040`, `rp2350-arm` | Podpisany kontener `.ota` JaszczurHAL | Zamiana program/staging zarządzana przez HAL, potwierdzenie próbne i rollback | Zweryfikowane sprzętowo na Pico W, Pico 2 W oraz Pico+PIM730/RM2 |
| `esp32s3` | Surowy plik BIN aplikacji ESP-IDF wybrany ze zwalidowanego manifestu buildu | Partycje aplikacji ESP-IDF `two-ota-large`, próba oczekująca na weryfikację, potwierdzenie i rollback | Implementacja oraz build/konsolidacja kompletne; weryfikacja sprzętowa/cyklu życia/negatywnych scenariuszy bezpieczeństwa w toku |

Protokół transportowy i publiczne wywołania zwrotne są współdzielone. Kontener
RP i obraz aplikacji ESP to różne artefakty i nigdy nie są konwertowane
jeden w drugi.

<a id="shared-auth2-transport-authentication"></a>

## Współdzielone uwierzytelnianie transportowe AUTH2

Przy skonfigurowanym niepustym haśle urządzenie i host używają odpornej na
awarie (fail-closed) wymiany `AUTH2`:

1. Host wysyła `0 <tcp-port> <image-size> <image-md5>` z jednego połączonego
   gniazda UDP. Urządzenie zapisuje adres IPv4 i port źródłowy tego gniazda.
2. Urządzenie generuje 16-bajtowy losowy nonce i odpowiada
   `AUTH2 <device-nonce>` na ten zapisany punkt końcowy.
3. Host generuje własny 16-bajtowy losowy nonce. Obie strony formatują
   dokładny transkrypt ASCII
   `JHOTA-AUTH-2:<command>:<tcp-port>:<image-size>:<image-md5>:<device-nonce>:<client-nonce>`.
   Pola szesnastkowe są normalizowane do małych liter.
4. Obie strony wyprowadzają klucz HMAC jako ASCII małymi literami
   `MD5(bajty UTF-8 hasła)` i obliczają HMAC-SHA256 na transkrypcie. Host
   wysyła `201 <client-nonce> <64-hex-character-tag>`.
5. Urządzenie akceptuje tę odpowiedź tylko z oryginalnego adresu UDP i portu
   źródłowego. Odpowiada `OK`, a następnie łączy się z tym samym adresem
   IPv4 na porcie TCP z uwierzytelnionego transkryptu. Host przesyła dane
   tylko wtedy, gdy adres peera TCP odpowiada adresowi wybranemu przez jego
   połączone gniazdo UDP.

Datagramy zaproszenia i AUTH2 używają jednej spacji ASCII między polami i nie
mają wiodących ani końcowych białych znaków. Wiadomość może kończyć się
bezpośrednio po ostatnim polu, jednym LF lub jednym CRLF. Pola liczbowe
używają najkrótszej postaci dziesiętnej; wejście szesnastkowe jest
akceptowane w dowolnej wielkości liter i normalizowane do małych liter przed
obliczeniem transkryptu. Osadzone NUL-e, dodatkowe linie, tabulatory,
zduplikowane separatory, aliasy liczbowe z wiodącymi zerami, zniekształcone
tagi i wartości poza zakresem są odrzucane.

Gdy host ma niepuste hasło, wymaga pełnej wymiany `AUTH2`; bezpośrednie `OK`,
starsze wyzwanie `AUTH` i starsza odpowiedź `200` nie mogą go zdegradować.
Wiązanie punktu końcowego odrzuca też odpowiedź na wyzwanie z innego adresu
UDP lub portu źródłowego oraz wywołanie zwrotne TCP z innego adresu. Nonce
urządzenia i klienta pochodzą z docelowego providera bezpiecznej losowości
(secure-random) oraz z CSPRNG systemu operacyjnego hosta. AUTH2 uwierzytelnia
dowód wyprowadzony z hasła oraz zaproszenie, ale nie szyfruje odkrywania
(discovery), metadanych ani firmware'u. Zaproszenie wiąże obraz przez MD5 dla
zgodności transportowej, więc walidacja obrazu specyficzna dla targetu pozostaje
niezbędna.

Pominięcie `hal_ota_set_password()` lub przekazanie pustego łańcucha sprawia,
że urządzenie akceptuje zaproszenia bez AUTH2. Host zezwala na ten tryb
tylko wtedy, gdy `ota.allowEmptyPassword` jest jawnie ustawione na `true`;
to ustawienie jest potwierdzeniem operatora i nie konfiguruje urządzenia.
Każdy peer sieciowy, który może dotrzeć do usługi UDP OTA, może wówczas
zainicjować transfer i dostarczyć obraz, który przejdzie pozostałą walidację
targetu. Ogranicz ten tryb do izolowanych sieci deweloperskich.

## Workflow ESP32-S3

Projekt ESP32-S3 włącza `HAL_ENABLE_OTA` w `hal_project_config.h`.
Wygenerowane domyślne ustawienia ESP-IDF wybierają tabelę partycji
`two-ota-large` i włączają rollback aplikacji. Pierwsza instalacja używa
normalnej, zweryfikowanej akcji programowania po USB, ponieważ urządzenie
potrzebuje pasującego bootloadera, tabeli partycji i początkowej aplikacji,
zanim aktualizacje sieciowe będą mogły zadziałać.

Użyj normalnego manifestu projektu ESP-IDF wraz ze współdzielonymi
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

Kod urządzenia konfiguruje współdzieloną usługę po tym, jak WiFi staje się
używalne, regularnie wywołuje `hal_ota_handle()` i potwierdza próbę dopiero
po tym, jak kontrole zdrowia produktu zakończą się powodzeniem:

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

`Project: Upload (OTA)` lub `jh-vscode upload-ota` wykonuje build
produkcyjny, wymaga `HAL_ENABLE_OTA` w rozwiązanym zestawie funkcji,
waliduje relokowalny manifest artefaktów ESP-IDF oraz sprawdza rozmiar i
SHA-256 pliku BIN aplikacji względem jego rekordu obrazu flash. Następnie
przesyła te surowe bajty aplikacji; nie podpisuje ani nie owija ich jako
kontener `.ota` RP. Odkrywanie, stałe `ota.host`, `listenPort`, `passwordEnv`
oraz zachowanie zapory sieciowej używają wspólnego workflow
hosta opisanego poniżej.

Urządzenie zapisuje nieaktywną partycję aplikacji OTA przez `esp_ota_*`,
sprawdza MD5 przesłanego obrazu oraz walidację aplikacji ESP-IDF, wybiera
nową partycję rozruchową i restartuje się. `hal_ota_get_boot_info_ex()`
mapuje stan partycji/obrazu ESP-IDF na publiczne tryby: stabilny, oczekujący,
próbny, rollback i odzyskiwanie. `hal_ota_confirm_boot_ex()` wywołuje
`esp_ota_mark_app_valid_cancel_rollback()` dla poprawnej próby. Jeśli
aplikacja nie potwierdzi, bootloader ESP-IDF wykonuje rollback zgodnie ze
skonfigurowaną polityką.

Używaj unikalnego, wysokoentropijnego hasła AUTH2 w wdrożonych systemach.
AUTH2 i MD5 transferu nie zapewniają nowoczesnego podpisywania obrazów ani
poufności. Secure Boot V2, szyfrowanie flash, polityka anti-rollback,
chronione klucze oraz procedury odzyskiwania to osobne kontrole produkcyjne.
Standardowe wgrywanie i testy nie mogą programować nieodwracalnych eFuse'ów.

Programowanie przez Serial/JTAG kompletnego, zwalidowanego manifestu
pozostaje ścieżką odzyskiwania, gdy WiFi, nowa aplikacja lub metadane OTA są
nieużywalne. Obecna implementacja OTA dla ESP32-S3 ma pokrycie tylko na
poziomie buildu/konsolidacji; próba/potwierdzenie/rollback, przerwane
transfery, przypadki nieprawidłowego obrazu/uwierzytelniania/transferu oraz
odzyskiwanie nadal wymagają fizycznej weryfikacji.

## Workflow RP

Natywne OTA na RP jest wspierane przez oficjalne targety Pico SDK `rp2040` i
`rp2350-arm`. Kompletna ścieżka WiFi została zweryfikowana na Pico W,
Pico 2 W oraz zwykłym Pico z dodatkiem PIM730/RM2, zarówno w buildach
bare-metal, jak i FreeRTOS.

Natywny workflow ma cztery odrębne etapy:

1. Zbuduj aplikację z `HAL_ENABLE_OTA`. CMake tworzy dwa sloty firmware'u,
   obszar kontrolny OTA, niepodpisany kontener `.ota` oraz połączony (merged)
   UF2 zawierający aplikator rozruchowy plus aplikację.
2. Zainstaluj ten połączony UF2 jednorazowo przez BOOTSEL. Czysta płytka nie
   może otrzymać aktualizacji sieciowej.
3. Odkryj lub zaadresuj działającą płytkę przez UDP. Host podpisuje kontener
   w chwili wgrywania skonfigurowanym hasłem.
4. Prześlij podpisany kontener do slotu staging. Płytka uruchamia obraz jako
   próbę, a kod aplikacji potwierdza go dopiero po tym, gdy kontrole
   rozruchu produktu zakończą się powodzeniem.

Połączony UF2 zawiera strony wypełnione zerami dla każdego dotkniętego,
niekońcowego sektora flash. Wynika to z obejścia (workaround) Pico SDK dla
[RP2040-E14](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf):
bez tego wypełnienia rzadka granica między aplikatorem rozruchowym a
aplikacją może sprawić, że BOOTSEL zaprogramuje niekompletny obraz.

## Manifest projektu RP

Włącz OTA w `.vscode/jaszczurhal.project.json`, opublikuj ścieżki
wygenerowanych artefaktów i zdefiniuj ustawienia odkrywania oraz
uwierzytelniania po stronie hosta:

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
| `JH_OTA_GENERATION` | Niepodpisana 32-bitowa generacja obrazu przechowywana w kontenerze. Zwiększaj ją zgodnie z polityką wydań projektu. To metadane, a nie wymuszany licznik anti-rollback. Wartość domyślna to `1`. |
| `JH_OTA_VERSION` | Czytelna dla człowieka wersja przechowywana w obrazie. Musi być krótsza niż 32 bajty UTF-8. Wartość domyślna to `dev`. |
| `artifacts.ota` | Preferowana dokładna ścieżka do niepodpisanego kontenera. Bez niej dispatcher akceptuje tylko jeden jednoznaczny plik `.ota` poniżej `buildDir`. |
| `artifacts.uf2` | Połączony obraz aplikatora rozruchowego i aplikacji używany do pierwszej instalacji i odzyskiwania przez USB. |

Obiekt `ota` kontroluje narzędzie hosta:

| Ustawienie | Znaczenie |
|---|---|
| `hostname` | Nazwa urządzenia używana do filtrowania wyników odkrywania. Musi odpowiadać `hal_ota_set_hostname()` w firmwarze, gdy używane jest odkrywanie. |
| `port` | Port UDP urządzenia do odkrywania, zaproszeń i uwierzytelniania. Musi odpowiadać `hal_ota_set_port()`. Wartość domyślna to `8266`. To nie jest port transferu danych TCP. |
| `listenPort` | Port callbacku TCP hosta ogłaszany urządzeniu. Wartość domyślna to `8266`, zgodna z trwałą regułą przygotowaną przez `runmefirst.sh`. Ustaw jawnie `0` tylko wtedy, gdy efemeryczny port callbacku i pasująca polityka zapory są zamierzone. |
| `passwordEnv` | Nazwa zmiennej środowiskowej hosta zawierającej hasło OTA. Ma pierwszeństwo przed `ota.password`. |
| `password` | Wbudowane hasło wyłącznie deweloperskie. Nie używaj go w śledzonym (tracked) manifeście produktu. |
| `allowEmptyPassword` | Zezwala na puste hasło, gdy jawnie ustawione na `true`. Domyślne zachowanie hosta odrzuca puste hasła. Nie włączaj tego dla wdrożonych urządzeń. |
| `broadcast` | Adres docelowy odkrywania. Wartość domyślna to `255.255.255.255`; skierowany broadcast, taki jak `192.168.2.255`, lub adres urządzenia dla odkrywania unicastowego, jest często bardziej niezawodny na hostach wielointerfejsowych. |
| `host` | Stały adres IPv4 urządzenia lub rozwiązywalna nazwa hosta. Wgrywanie omija odkrywanie przez broadcast, podczas gdy komenda odkrywania wysyła bezpośrednie zapytanie unicastowe. Nadaje się do automatyzacji i sieci routowanych. `--host` w wierszu poleceń nadpisuje to dla jednego wywołania. |

Używaj unikalnej nazwy hosta dla każdego jednocześnie zasilanego urządzenia.
Wybór urządzenia dopasowuje też aktywny target dispatchera. Gdy widoczne jest
więcej niż jedno pasujące urządzenie, użyj wyboru interaktywnego lub stałego
`ota.host`; automatyzacja nie może zgadywać.

## Sekret i konfiguracja po stronie urządzenia RP

Hasło istnieje po obu stronach workflow:

- firmware przekazuje je do `hal_ota_set_password()`;
- host odczytuje identyczny łańcuch z `ota.passwordEnv` i używa go do
  uwierzytelniania transportowego i podpisywania kontenera.

Nie umieszczaj hasła produktu w `app.c`, śledzonym manifeście, ustawieniach
VS Code ani śledzonym zadaniu. Prosty układ deweloperski używa śledzonego
szablonu i lokalnego, ignorowanego nagłówka:

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

Skopiuj szablon do `ota_secrets.h`, podmień wartości lokalnie i dołącz ten
plik z firmware'u. Dla produktów z provisioningiem hasło może zamiast tego
pochodzić z bezpiecznego magazynu specyficznego dla produktu, zanim
wywołane zostanie `hal_ota_begin()`. JaszczurHAL nie zapewnia chronionego
magazynu kluczy na RP2040/RP2350; sekret wkompilowany do firmware'u może
zostać odzyskany przez atakującego z wystarczającym dostępem fizycznym.

Wyeksportuj to samo hasło w powłoce, która uruchamia dispatchera:

```bash
export TRACKER_OTA_PASSWORD='the-same-value-used-by-firmware'
```

Zadania VS Code dziedziczą środowisko procesu VS Code, a nie eksporty
wykonane później w niepowiązanym zintegrowanym terminalu. Na Linuksie zamknij
istniejące procesy VS Code i uruchom projekt ze skonfigurowanej powłoki przy
zmianie zmiennej:

```bash
export TRACKER_OTA_PASSWORD='the-same-value-used-by-firmware'
code .
```

Wartość `passwordEnv` to tylko nazwa zmiennej; nie wpisuj
`"${TRACKER_OTA_PASSWORD}"` w manifeście.

## Integracja firmware'u RP

Skonfiguruj nazwę hosta, port UDP, hasło i opcjonalne wywołania zwrotne
przed uruchomieniem usługi OTA. Uruchom usługę dopiero po tym, jak sieć
stanie się używalna, wywołuj `hal_ota_handle()` często i potwierdzaj próbę
dopiero po tym, jak wszystkie kontrole gotowości specyficzne dla produktu
przejdą pomyślnie.

Poniższy szkielet pokazuje kompletny przepływ sterowania po stronie
aplikacji:

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

Bezwarunkowe `true` w `application_startup_checks_passed()` to tylko
placeholder. Produkt powinien uwzględniać każdy warunek wymagany do uznania
nowego obrazu za bezpieczny: zgodność konfiguracji, zamontowanie pamięci,
wymagany sprzęt, usługi sieciowe i dowolny autotest aplikacji. Zbyt
wczesne potwierdzenie usuwa ochronę rollback dla późniejszych awarii
rozruchu.

Dodatkowe reguły API:

- `hal_ota_set_port()` odrzuca port zero i nie może zmienić portu po
  uruchomieniu usługi.
- Nazwa hosta musi być niepusta. Domyślna nazwa hosta, gdy żadna nie jest
  podana, to nazwa targetu HAL.
- Pominięcie hasła urządzenia lub ustawienie go na pusty łańcuch pomija
  AUTH2. Host odrzuca ten tryb, chyba że `allowEmptyPassword` jest jawnie
  ustawione. Zawsze ustawiaj niepusty sekret produktu.
- `hal_ota_handle()` wykonuje obsługę sieciową, przetwarza odkrywanie,
  uwierzytelnianie i transfer oraz wysyła wywołania zwrotne. Nie przestawaj
  go wywoływać, dopóki OTA jest włączone.
- Niepowodzenie alokacji mutexu backendu utrzymuje usługę zatrzymaną:
  operacje boolowskie zwracają `false`, operacje statusowe zwracają
  `HAL_ENOMEM`, a `hal_ota_handle()` nie wykonuje żadnej pracy.
- Urządzenie restartuje się automatycznie po zaakceptowaniu i zwalidowaniu
  kompletnego obrazu.
- `hal_ota_get_boot_info_ex()` raportuje stan stabilny, próbny, rollback i
  odzyskiwania wraz z generacją, wersją, liczbą prób i limitem prób.
- `HAL_RP_OTA_MAX_BOOT_ATTEMPTS` domyślnie wynosi `3` i akceptuje wartości
  od 1 do 255. Nadpisuj go przez `JH_EXTRA_DEFINES` lub
  `hal_project_config.h` tylko wtedy, gdy produkt ma świadomą politykę
  rozruchu.

Dla aplikacji FreeRTOS uruchom usługę z jednego zadania i dobierz rozmiar
tego zadania pod inicjalizację CYW43 i przetwarzanie OTA. Sprzętowy zestaw
testów regresyjnych używa 2048 słów stosu FreeRTOS, czyli 8 KiB na RP:

```c
/* hal_project_config.h */
#pragma once

#if defined(HAL_ENABLE_FREERTOS) && !defined(HAL_FREERTOS_TASK0_STACK)
#define HAL_FREERTOS_TASK0_STACK 2048u
#endif
```

Zmierz rzeczywisty najwyższy poziom wykorzystania stosu (high-water mark)
finalnego produktu, zamiast zakładać, że ta wartość jest uniwersalnie
wystarczająca.

## Artefakty buildu RP i pierwsza instalacja

Sprawdź rozwiązany target, płytkę, ścieżki i ustawienia OTA przed pierwszym
buildem:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  config-dump --project "$PWD"
```

Zbuduj z katalogu projektu firmware'u:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  build --project "$PWD"
```

Build natywny RP z włączonym OTA produkuje:

| Artefakt | Przeznaczenie |
|---|---|
| `firmware.elf` | Symbole debugowania i inspekcja mapy pamięci. |
| `firmware.bin` | Surowy obraz aplikacji zlinkowany do slotu programu. |
| `firmware.ota` | Niepodpisany, jawny (plaintext) kontener OTA. Zawiera target, przesunięcie ładowania, generację, wersję, SHA-256 payloadu oraz CRC nagłówka, ale nie hasło. |
| `firmware.uf2` | Połączony aplikator rozruchowy i aplikacja do pierwszej instalacji lub odzyskiwania przez USB. |
| `firmware.signed.ota` | Artefakt tworzony w chwili wgrywania przez `upload-ota`; nadal jest jawny (plaintext) i zwykle przechowywany poniżej `buildDir`. |

Kontener v1 zaczyna się od tego 160-bajtowego nagłówka little-endian:

| Zakres bajtów | Pole |
|---|---|
| `0..7` | Magia `JHOTA1\r\n` |
| `8..9` | Wersja nagłówka, obecnie `1` |
| `10..11` | Rozmiar nagłówka, obecnie `160` |
| `12..13` | ID targetu: `1` RP2040, `2` RP2350 Arm, `3` RP2350 RISC-V |
| `14..15` | Zarezerwowane |
| `16..19` | Przesunięcie flash slotu programu |
| `20..23` | Rozmiar payloadu |
| `24..27` | Monotoniczna generacja obrazu |
| `28..31` | Flagi |
| `32..63` | SHA-256 payloadu |
| `64..95` | Wersja UTF-8, wypełniona NUL-ami; zakodowana wartość musi być krótsza niż 32 bajty |
| `96..127` | HMAC-SHA256 |
| `128..155` | Zarezerwowane |
| `156..159` | CRC32 bajtów nagłówka `0..155` |

Pakowanie pozostawia pole HMAC wyzerowane. Wgrywanie weryfikuje skrót
payloadu, oblicza szesnastkowy MD5 hasła UTF-8 zapisany małymi literami
ASCII i używa tych 32 bajtów ASCII jako klucza HMAC-SHA256 dla bajtów
nagłówka `0..95`. Zapisuje skrót w bajtach `96..127` i przelicza CRC32.
To wyprowadzenie klucza zachowuje protokół transportowy OTA; nie jest to
konstrukcja wzmacniająca hasło (password-hardening). Urządzenie weryfikuje
granice targetu/layoutu, CRC nagłówka, HMAC i skrót payloadu przed
oznaczeniem staging jako oczekujący.

OTA rezerwuje 16 KiB obszaru rozruchowego, równe sloty programu i staging
oraz cztery sektory kontrolne po 4 KiB przed dowolnym ogonem
LittleFS/EEPROM. Dostępna przestrzeń aplikacji jest zatem mniejsza niż w
buildzie bez OTA. CMake oblicza layout dla wybranego rozmiaru flash i kończy
się niepowodzeniem, jeśli obszary się nakładają lub aplikacja się nie
mieści.

Dla czystej płytki:

1. Przytrzymaj BOOTSEL podczas podłączania lub resetowania płytki.
2. Upewnij się, że widoczny jest tylko zamierzony dysk BOOTSEL RP.
3. Uruchom:

   ```bash
   ../libraries/JaszczurHAL/vscode/entry/jh-vscode \
     upload-uf2 --project "$PWD"
   ```

Normalna akcja `upload` również zapisuje UF2 przez USB, ale może użyć
resetu CDC 1200-bps firmware'u dopiero po tym, jak firmware zostało już
raz zainstalowane. `upload` nie jest komendą OTA. Ręczny BOOTSEL pozostaje
ścieżką odzyskiwania, gdy aplikacja, WiFi lub usługa OTA nie mogą się
uruchomić.

## Odkrywanie i wgrywanie OTA na RP

Poczekaj, aż firmware dołączy do WiFi i `hal_ota_begin()` zakończy się
powodzeniem, następnie wylistuj urządzenia:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  ota-discover --project "$PWD"
```

Odkrywanie w formacie maszynowym jest dostępne z `--json`. Odpowiedź
odkrywania zawiera nazwę hosta urządzenia, adres, target, port UDP, rozmiar
slotu, aktywną generację i tryb rozruchu.

Wgraj interaktywnie:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" --interactive
```

Lub omiń odkrywanie dla znanego adresu:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" --host 192.168.2.200
```

`upload-ota` zawsze najpierw buduje, lokalizuje niepodpisany artefakt
`.ota`, odczytuje hasło, tworzy `firmware.signed.ota`, uwierzytelnia
zaproszenie, przesyła obraz, czeka na akceptację urządzenia i raportuje,
że urządzenie się restartuje. Automatycznie wybiera urządzenie tylko
wtedy, gdy dokładnie jeden wynik odkrywania pasuje do aktywnego targetu i
skonfigurowanej nazwy hosta.

Dla buildów z wieloma kombinacjami target/płytka wybierz zamierzony profil
najpierw lub przekaż nadpisanie jawnie:

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode \
  upload-ota --project "$PWD" \
  --target rp2350-arm --board pico2w --host 192.168.2.200
```

`--variant` ma zastosowanie tylko wtedy, gdy manifest deklaruje ten
wariant. Na przykład zadeklarowany wariant `freertos` jest wybierany przez
`--variant freertos`.

## Zadania VS Code i skróty klawiszowe dla RP

Wygenerowane projekty zawierają te utrzymywane zadania:

| Zadanie | Przeznaczenie |
|---|---|
| `Project: Build` | Zbuduj wszystkie artefakty wybranego targetu, w tym `.ota` i połączony UF2. |
| `Project: Upload (UF2 / BOOTSEL)` | Pierwsza instalacja lub odzyskiwanie przez USB. |
| `Project: Discover OTA devices` | Uruchom odkrywanie OTA i wypisz pasujące urządzenia. |
| `Project: Upload (OTA)` | Zbuduj, wybierz urządzenie interaktywnie, podpisz i wgraj przez sieć. |
| `Project: Config Dump` | Sprawdź rozwiązany manifest oraz lokalny wybór targetu/płytki. |

Migrowane projekty powinny skopiować bieżące definicje zadań z
[`vscode/examples/tasks.json`](../../vscode/examples/tasks.json). Zadania
wywołują `${config:jaszczurhal.vscodeEntry}`, więc `.vscode/settings.json`
musi wskazywać tym ustawieniem na checkout JaszczurHAL projektu.

Odpowiadające referencyjne skróty to:

| Skrót | Zadanie |
|---|---|
| `Ctrl+Shift+1` | `Project: Build` |
| `Ctrl+Shift+4` | `Project: Upload (UF2 / BOOTSEL)` |
| `Ctrl+Shift+8` | `Project: Upload (OTA)` |
| `Ctrl+Shift+9` | `Project: Config Dump` |
| `Ctrl+Shift+Alt+3` | `Project: Discover OTA devices` |

Pliki `.vscode/keybindings.reference.json` projektu są wyłącznie
dokumentacją. VS Code nie ładuje lokalnych dla repozytorium powiązań
klawiszy. Dodaj wpisy do rzeczywistego pliku powiązań klawiszy użytkownika
przez **Preferences: Open Keyboard Shortcuts (JSON)**:

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
`~/.config/Code/User/keybindings.json`. Przeładuj okno po zmianie
powiązań zadań. Nie myl skrótów z katalogu głównego repozytorium, które
działają na samej bibliotece JaszczurHAL, ze skrótami projektu firmware'u.

## Przepływ sieciowy i zapora hosta dla RP

Połączenie danych OTA jest celowo odwrócone:

1. Host wysyła pakiety odkrywania, zaproszenia i uwierzytelniania przez UDP
   do skonfigurowanego portu OTA urządzenia, zwykle `8266`.
2. Host otwiera nasłuchiwacz TCP. `ota.listenPort` wybiera jego port;
   wartość domyślna to `8266`, natomiast jawna wartość `0` prosi system
   operacyjny o port efemeryczny.
3. Zaproszenie informuje urządzenie o tym porcie TCP.
4. Urządzenie inicjuje nowe połączenie TCP z powrotem do hosta i odbiera
   obraz w potwierdzanych fragmentach.

Przy `"listenPort": 8266` SYN z urządzenia celuje w port TCP 8266 hosta,
więc wystarczy reguła zapory dla tego dokładnego portu callbacku. Przy
`"listenPort": 0` Linux zwykle wybiera port z
`/proc/sys/net/ipv4/ip_local_port_range`, a polityka zapory musi
obejmować ten wybrany port efemeryczny. Punkt dostępowy lub sieć routowana
musi też zezwalać na ruch urządzenie-do-hosta; wyłącz izolację klientów
bezprzewodowych dla sieci OTA.

`runmefirst.sh` wykrywa sieć RFC1918 podłączoną do domyślnego interfejsu
IPv4 i sprawdza obecność trwałej reguły callbacku TCP/8266. Na Windows
uruchom tego samego, skoncentrowanego pomocnika Python z zarządzanego
środowiska. Gdy reguła jest nieobecna, wyświetla dokładny interfejs,
podsieć źródłową, port, backend trwałości oraz każdą granicę pakietu lub
podniesienia uprawnień przed poproszeniem o potwierdzenie. Odmowa
pozostawia zaporę bez zmian i nie blokuje pozostałej części konfiguracji.
Host z pustym łańcuchem `INPUT` i polityką `ACCEPT` już zezwala na
callback, więc konfiguracja raportuje sukces bez instalowania narzędzi
trwałości.

Po potwierdzeniu konfiguracja używa aktywnego menedżera zapory:

- aktywny UFW otrzymuje trwałą regułę ograniczoną do interfejsu i podsieci;
- aktywny firewalld otrzymuje pasujące reguły rich rule w trybie runtime i
  permanent;
- host `iptables-nft`/`iptables` otrzymuje wczesną regułę `INPUT`,
  utrwaloną przez `netfilter-persistent`, której usługa startowa jest
  włączana, gdy dostępny jest systemd;
- `iptables-persistent` jest instalowane przez `apt` tylko wtedy, gdy
  ścieżka iptables wymaga trwałości i żadne wspierane narzędzie trwałości
  nie jest obecne.
- Zapora Windows Defender Firewall otrzymuje nazwaną regułę przychodzącą
  ograniczoną do profilu `Private`, wybranego aliasu interfejsu, podsieci
  źródłowej RFC1918, TCP i portu callbacku. Pomocnik nie zmienia sieci
  `Public` na `Private`.

Konfiguracja nigdy nie włącza nieaktywnej polityki UFW ani firewalld.
Ścieżka trwałości iptables zapisuje kompletny aktywny zestaw reguł IPv4 do
`/etc/iptables/rules.v4` bez zapisywania polityki IPv6, co jest podawane
w monicie potwierdzającym. Uruchom ponownie skoncentrowanego pomocnika po
zmianie LAN, interfejsu lub portu callbacku:

```bash
python3 scripts/configure_ota_firewall.py
python3 scripts/configure_ota_firewall.py --check
python3 scripts/configure_ota_firewall.py --dry-run
python3 scripts/configure_ota_firewall.py \
  --interface enp7s0 --network 192.168.2.0/24
```

Natywny Windows używa zarządzanego interpretera oraz aliasu interfejsu
pokazywanego przez `Get-NetConnectionProfile`:

```powershell
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --dry-run `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
.\.build\windows\venv\Scripts\python.exe `
  .\scripts\configure_ota_firewall.py --check `
  --interface 'Wi-Fi' --network '192.168.2.0/24'
```

Jeśli sprawdzone połączenie to `Public`, wybierz zaufaną sieć LAN i
świadomie zmień jej profil przez ustawienia Windows przed skonfigurowaniem
reguły. Stosuj z już podniesionego (elevated) PowerShell dopiero po
przejrzeniu `--dry-run`. Skrypt raportuje granicę podniesienia uprawnień i
sam nie wywołuje UAC.

Pomocnik akceptuje wyłącznie źródłowe sieci IPv4 RFC1918. `--yes` zezwala
na nieinteraktywną konfigurację po jawnym wybraniu interfejsu i sieci.
Konfiguracja odmawia wystawienia portu, gdy inny proces już na nim
nasłuchuje. Uploader tworzy nasłuchiwacz TCP tylko na czas trwania
transferu; trwała reguła zapory nie uruchamia usługi w tle.

Określ trasę, interfejs callbacku i aktywną implementację zapory:

```bash
jh_ota_device_ip=192.168.2.200
ip route get "$jh_ota_device_ip"
sudo nft list ruleset
sudo iptables-save
```

Użyj wartości `dev` wypisanej przez `ip route get` jako interfejsu
wejściowego. Adres hosta w wyjściu trasy to adres docelowy dla callbacku.
Dla stałego nasłuchiwacza zweryfikuj, że proces wgrywania nasłuchuje, i
przechwyć handshake:

```bash
ss -ltn 'sport = :8266'
sudo tcpdump -ni enp7s0 \
  'host 192.168.2.200 and (udp port 8266 or tcp port 8266)'
```

Jeśli przechwycenie pokazuje powtarzające się pakiety SYN urządzenia bez
SYN-ACK, callback dotarł do hosta, a pozostałą granicą jest zapora hosta
lub nasłuchiwacz. Jeśli żaden SYN się nie pojawia po pomyślnym
uwierzytelnieniu UDP, sprawdź ogłaszany adres callbacku, trasę i izolację
AP.

Na hoście nftables zarządzanym przez warstwę zgodności `iptables-nft`,
pomocnik instaluje równoważną trwałą regułę. Wyłącznie do ręcznej
diagnostyki, tymczasowa reguła ograniczona do hosta źródłowego może zostać
wstawiona po potwierdzeniu aktywnego łańcucha `INPUT` w `nft list ruleset`
lub `iptables-save`:

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

Zamień interfejs i oba adresy na wartości z trasy. Sprawdź licznik za
pomocą:

```bash
sudo /usr/sbin/iptables-nft \
  -L INPUT -n -v --line-numbers
```

Usuń tymczasową regułę z tym samym kompletnym dopasowaniem i `-D` w
miejscu `-I ... 1`. Bezpośrednie zmiany łańcucha zgodności mogą zniknąć
po przeładowaniu zapory lub restarcie. Do normalnej trwałej konfiguracji
używaj pomocnika.

Gdy `listenPort` wynosi zero, sprawdź wybrany nasłuchiwacz przez
`ss -ltnp` i odczytaj zakres hosta za pomocą:

```bash
cat /proc/sys/net/ipv4/ip_local_port_range
```

Normalna zapora stanowa zezwala na odpowiedź UDP na wychodzący pakiet
odkrywania lub zaproszenia hosta. Restrykcyjna polityka wychodząca musi
dodatkowo zezwalać na UDP host-do-urządzenia na skonfigurowanym porcie
OTA oraz jego ruch odpowiedzi. Dla samego `jh-vscode` nie jest wymagane
`sudo`.

Jeśli odkrywanie przez broadcast jest zablokowane, ale adres jest znany,
użyj `ota.host`, `--host` lub wartości unicastowej `ota.broadcast`.
Omija to tylko broadcast; nie usuwa wymogu zapory dla callbacku TCP.

## Potwierdzenie próbne, rollback i odzyskiwanie na RP

Po udanym transferze aplikator rozruchowy zamienia staging na slot
programu i uruchamia go w `HAL_OTA_BOOT_TRIAL`. Każdy niepotwierdzony
rozruch zwiększa licznik prób. Gdy zapisany limit zostanie osiągnięty,
aplikator rozruchowy zamienia z powrotem poprzedni obraz i uruchamia go
jako stabilny.

Wywołuj `hal_ota_confirm_boot_ex()` dopiero po tym, jak nowy obraz przejdzie
swoje kryteria rozruchu. Wywołanie go, gdy jest już stabilny, jest
nieszkodliwe. Używaj `hal_ota_get_boot_info_ex()` w diagnostyce, aby dzienniki
mogły odróżnić normalny stabilny rozruch od próbnego, rollbacku lub
odzyskiwania.

Zachowaj ścieżkę odzyskiwania przez USB:

- BOOTSEL plus połączony `firmware.uf2` mogą ponownie zainstalować
  aplikator rozruchowy i aplikację, gdy rozruch sieciowy jest uszkodzony.
- Zmiana rozmiaru flash, layoutu OTA/pamięci lub targetu może pozostawić
  niekompatybilne sektory stanu. Reprowizjonuj lub wymaż odpowiednie
  urządzenie dopiero po zachowaniu wszelkich wymaganych danych
  LittleFS/EEPROM.
- Komendy kasowania sektorów kontrolnych specyficzne dla targetu w
  [Sprzętowej sondzie natywnego OTA RP](../api/pl/03_build_tests.md#sprzętowy-test-natywnego-ota-na-rp)
  dotyczą dokładnego layoutu tego zestawu testowego i nie są uniwersalnymi
  zakresami kasowania dla produktu.
- Fizyczny dostęp do BOOTSEL pozostaje poza granicą zaufania OTA.

## Granica bezpieczeństwa RP

Podpisany kontener uwierzytelnia swój wersjonowany nagłówek przez
HMAC-SHA256 oraz weryfikuje SHA-256 payloadu i CRC nagłówka przed
aktywacją. To samo hasło uwierzytelnia
[współdzielony transport AUTH2](#współdzielone-uwierzytelnianie-transportowe-auth2).
Zapewnia to uwierzytelnianie i integralność, nie poufność. HMAC-SHA256
obrazu RP i SHA-256 payloadu pozostają niezależnymi kontrolami wykonywanymi
przed zaakceptowaniem staging; AUTH2 ich nie zastępuje.

- firmware i oba artefakty `.ota` są jawne (plaintext);
- każdy, kto zna hasło, może stworzyć zaakceptowany obraz;
- generacja obrazu nie jest licznikiem anti-rollback, więc starszy
  poprawnie podpisany obraz może zostać odtworzony (replay), chyba że
  produkt dodaje własną politykę;
- używaj unikalnego, wysokoentropijnego hasła na produkt lub grupę
  urządzeń;
- wykonuj OTA wyłącznie w zaufanym segmencie sieci lub dodaj specyficzną
  dla produktu granicę szyfrowanego transportu/VPN.

Zobacz [Łańcuch dostaw bezpieczeństwa](security_supply_chain.md#native-ota-security-boundary)
po aktualne stwierdzenie dotyczące bezpieczeństwa.

## Lista kontrolna rozwiązywania problemów RP

Jeśli odkrywanie lub wgrywanie zawiedzie, sprawdź je w tej kolejności:

1. `config-dump` raportuje zamierzony natywny target RP, płytkę WiFi, nazwę
   hosta, port UDP, nazwę zmiennej środowiskowej hasła i ścieżki
   artefaktów.
2. Firmware zostało zbudowane z `HAL_ENABLE_OTA`; `firmware.ota` i
   połączony `firmware.uf2` istnieją poniżej rozwiązanego `buildDir`.
   Dla RP2040 każdy dotknięty sektor przed ostatnią stroną UF2 musi
   zawierać wszystkie szesnaście stron po 256 bajtów;
   `test_rp_ota_artifacts` wymusza ten layout.
3. Firmware dołączyło do WiFi, `hal_ota_begin()` zwróciło true, a
   `hal_ota_handle()` nadal działa.
4. Firmware i host używają tego samego portu, nazwy hosta i dokładnie
   tych samych bajtów hasła. Uruchom ponownie VS Code ze skonfigurowanego
   środowiska, jeśli `passwordEnv` jest raportowane jako brakujące.
5. Aktywny target odpowiada odkrytemu urządzeniu. Użyj `--interactive` dla
   kilku pasujących urządzeń i `--host` dla znanego adresu.
6. Broadcast dociera do właściwego interfejsu. Preferuj skierowany
   broadcast lub bezpośredni host na hostach wielointerfejsowych i
   routowanych.
7. AP zezwala na ruch urządzenie-do-hosta, a licznik zapory hosta rośnie,
   gdy płytka rozpoczyna swój callback TCP. Uruchom ponownie
   `scripts/configure_ota_firewall.py`, jeśli interfejs lub LAN się
   zmieniły.
8. Powtarzające się niepowodzenie uwierzytelniania zwykle oznacza
   niezgodność hasła. Timeout akceptacji TCP po pomyślnym uwierzytelnieniu
   UDP zwykle oznacza błędną regułę zapory callbacku lub trasę.
9. Natychmiastowy rollback oznacza, że aplikacja nie potwierdziła próby
   przed osiągnięciem limitu prób lub że jej kontrole gotowości nigdy nie
   przeszły pomyślnie.
10. Płytka przeniesiona między niekompatybilnymi obrazami
    target/runtime/layout może wymagać kontrolowanego
    reprowizjonowania przez USB lub czyszczenia metadanych OTA.
