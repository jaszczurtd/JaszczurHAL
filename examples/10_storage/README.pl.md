<a id="10---pamięć-masowa"></a>

# 10 - Zapis danych w pamięci flash i na karcie SD

Przykład zapisuje i odczytuje dane w magazynie klucz-wartość (KV), obsługuje
partycję LittleFS oraz zapisuje dziennik na karcie SD przez SDLogger.
Błędy są obsługiwane osobno, ale SDLogger wymaga poprawnie uruchomionych
EEPROM i SPI. LittleFS nie zależy od inicjalizacji magazynu KV.

Magazyn KV przy starcie kasuje zapasowy bank przez `hal_kv_prepare_ex()`,
zwiększa trwały licznik uruchomień, zapisuje nazwę urządzenia jako rekord
binarny, zatwierdza zmiany i sprawdza odczytaną nazwę. Po zamontowaniu
LittleFS aplikacja sprawdza, czy istnieje
`/hal_marker.txt`, i usuwa go, jeśli jest obecny. Nie tworzy przy tym nowych
plików. SDLogger prowadzi dziennik i zapisuje jednorazowy raport startowy.

## Rozmieszczenie danych

Na platformach RP i STM32 aplikacja przekazuje `0u` jako rozmiar do
`hal_eeprom_init()`, pozostawiając wybór rozmiaru konfiguracji HAL. Magazyn KV
zaczyna się od `KV_BASE_ADDR=0u`, a jego rozmiar określa odpowiednio
`HAL_RP_FLASH_EEPROM_SIZE` lub `HAL_STM32_FLASH_EEPROM_SIZE`.
Dla pozostałej gałęzi kodu rozmiar EEPROM i KV wynosi 8192 bajty.

Rozmieszczenie partycji LittleFS i trwałych danych SDLogger zależy od
konfiguracji oraz implementacji HAL. Sprawdź je przed przeznaczeniem
własnych obszarów pamięci na dane aplikacji.

## Karta SD

Karta korzysta z magistrali SPI 0. Połączenia każdego montażu opisuje
`device_tree.json`, a program bierze magistralę, jej piny i linię CS karty
z wygenerowanego nagłówka. Poniższa tabela powtarza te połączenia.

| Sygnał karty SD | Pico / Pico 2 | NUCLEO-G474RE |
|---|---|---|
| MISO | GPIO16 | PA6, pin 13 CN10 / D12 |
| MOSI | GPIO19 | PA7, pin 15 CN10 / D11 |
| SCK | GPIO18 | PB3, pin 31 CN10 / D3 |
| CS | GPIO17 | PB6, pin 17 CN10 / D10 |

Na NUCLEO-G474RE zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje
diodą LD2.

Po błędzie operacji SDLogger aplikacja zamyka bieżący dziennik przez publiczne
API i co pięć sekund ponawia inicjalizację. Nieudany zapis raportu startowego
obsługuje oddzielnie: zamyka go i podejmuje kolejną próbę.

## Formatowanie LittleFS

Formatowanie jest domyślnie wyłączone, aby błąd montowania nie spowodował
utraty danych. Ustaw `EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT=1`
w `hal_project_config.h`, bezpośrednio lub przez wariant, tylko wtedy,
gdy dopuszczasz wymazanie zarezerwowanej partycji LittleFS.
