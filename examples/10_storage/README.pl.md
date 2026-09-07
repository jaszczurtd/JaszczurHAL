<a id="10---pamięć-masowa"></a>

# 10 - Zapis danych w pamięci flash i na karcie SD

Przykład zapisuje i odczytuje dane w magazynie klucz-wartość (KV), obsługuje
partycję LittleFS oraz zapisuje dziennik na karcie SD przez SDLogger.
Błędy są obsługiwane osobno, ale SDLogger wymaga poprawnie uruchomionych
EEPROM i SPI. LittleFS nie zależy od inicjalizacji magazynu KV.

Magazyn KV zwiększa trwały licznik uruchomień, zapisuje nazwę urządzenia jako
rekord binarny, zatwierdza zmiany i sprawdza odczytaną nazwę. Po zamontowaniu
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

Karta korzysta z SPI0. Połączenia MISO/MOSI/SCK/CS to GPIO 16/19/18/17
na płytkach RP oraz PA6/PA7/PA5/PB6 na NUCLEO-G474RE. Na NUCLEO są to
odpowiednio piny 13/15/11/17 złącza CN10, czyli D12/D11/D13/D10.

Po błędzie operacji SDLogger aplikacja zamyka bieżący dziennik przez publiczne
API i co pięć sekund ponawia inicjalizację. Nieudany zapis raportu startowego
obsługuje oddzielnie: zamyka go i podejmuje kolejną próbę.

## Formatowanie LittleFS

Formatowanie jest domyślnie wyłączone, aby błąd montowania nie spowodował
utraty danych. Ustaw `EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT=1`
w `hal_project_config.h` albo jako definicję kompilatora tylko wtedy,
gdy dopuszczasz wymazanie zarezerwowanej partycji LittleFS.
