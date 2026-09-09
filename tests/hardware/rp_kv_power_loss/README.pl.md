# Sprzętowy test utraty zasilania podczas zapisu KV na RP

Stanowisko zapisuje do natywnej pamięci flash RP kontrolowane, niekompletne
banki KV po kasowaniu, zapisie treści i jej weryfikacji. Ponowne wczytanie
fizycznej pamięci do kopii EEPROM modeluje restart. Test sprawdza powrót do
poprzedniego kompletnego banku oraz odzyskanie nowszego banku, gdy błąd został
zgłoszony już po pełnej publikacji. Sprawdza też, czy funkcje odczytujące
w trybie kontroli nośnika (ang. read-through) odrzucają obraz RAM
z niezatwierdzonymi zmianami statusem `HAL_EBUSY`, zerują wyjściową wartość
skalarną i długość bloba, a po zatwierdzeniu odczytują nowe wartości zarówno
przed, jak i po ponownym załadowaniu danych z fizycznej pamięci flash.
Przełącznika fault injection używanego wyłącznie podczas budowania stanowiska
nie wolno włączać w firmware produkcyjnym.
