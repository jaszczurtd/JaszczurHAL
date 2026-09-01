# 10 - Pamięć masowa

Ten przykład sprawdza trzy niezależne mechanizmy przechowywania danych:

- EEPROM i magazyn KV oparte na pamięci flash,
- partycję flash LittleFS,
- SDLogger przez SPI wraz z jednorazowym raportem startowym.

Magazyn KV zapisuje binarny rekord z nazwą urządzenia, a następnie go odczytuje.
Po zamontowaniu LittleFS przykład sprawdza obecność `/hal_marker.txt` i usuwa
plik, jeśli istnieje. W ten sposób testuje fasady sprawdzania ścieżki i usuwania
bez tworzenia nowych plików.

Błąd jednego mechanizmu nie zatrzymuje pozostałych. EEPROM udostępnia w czasie
działania 1024 bajty.
SDLogger zajmuje bajty 0-7, a magazyn KV bajty 64-575, więc rekordy nie mogą na
siebie nachodzić. Natywne konfiguracje rezerwują oddzielne fizyczne obszary flash:
domyślny obszar EEPROM 4 KiB i obszar LittleFS 64 KiB.

Karta SD używa SPI0. Targety RP używają MISO/MOSI/SCK/CS GPIO 16/19/18/17.
NUCLEO-G474RE używa PA6/PA7/PA5/PB6: MISO jest na pinie 13 CN10 (D12), MOSI na
pinie 15 CN10 (D11), SCK na pinie 11 CN10 (D13), a CS na pinie 17 CN10 (D10).
Po błędzie SDLogger zamyka bieżący dziennik przez publiczne API, a następnie co
pięć sekund ponawia inicjalizację. Po nieudanym zapisie raportu startowego usuwa
niekompletne dane i niezależnie ponawia próbę.

Formatowanie LittleFS jest domyślnie wyłączone, aby błąd montowania nie
doprowadził do wymazania danych z istniejącej partycji. Ustaw
`EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT=1` w
`hal_project_config.h` albo jako definicję kompilatora wyłącznie wtedy, gdy
wymazanie zarezerwowanej partycji LittleFS jest jawnie dopuszczalne.
