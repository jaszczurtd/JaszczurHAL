# 10 - Storage

Ten przykład sprawdza trzy niezależne ścieżki storage:

- EEPROM i magazyn KV oparte na flash,
- partycję flash LittleFS,
- SDLogger przez SPI wraz z jednorazowym raportem startowym.

Ścieżka KV zapisuje blob nazwy urządzenia i odczytuje go z powrotem. Po
zamontowaniu LittleFS przykład sprawdza obecność `/hal_marker.txt` i usuwa plik,
jeśli istnieje. Sprawdza to fasady zapytania o ścieżkę i usuwania bez tworzenia
dowolnych plików.

Błąd jednej ścieżki nie zatrzymuje pozostałych. EEPROM ma w runtime 1024 bajty.
SDLogger zajmuje bajty 0-7, a magazyn KV bajty 64-575, więc rekordy nie mogą na
siebie nachodzić. Buildy native rezerwują oddzielne fizyczne obszary flash:
domyślny obszar EEPROM 4 KiB i obszar LittleFS 64 KiB.

Karta SD używa SPI0. Targety RP używają MISO/MOSI/SCK/CS GPIO 16/19/18/17.
NUCLEO-G474RE używa PA6/PA7/PA5/PB6: MISO jest na pinie 13 CN10 (D12), MOSI na
pinie 15 CN10 (D11), SCK na pinie 11 CN10 (D13), a CS na pinie 17 CN10 (D10).
Po błędzie SDLogger bieżący log jest zamykany przez publiczne API, a inicjalizacja
ponawiana co pięć sekund. Nieudany zapis raportu startowego jest sprzątany i
ponawiany niezależnie.

Formatowanie LittleFS jest domyślnie wyłączone, aby błąd montowania nie usunął
istniejącej partycji. Ustaw `EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT=1` w
`hal_project_config.h` albo jako definicję kompilatora wyłącznie wtedy, gdy
usunięcie zarezerwowanej partycji LittleFS jest jawnie dopuszczalne.
