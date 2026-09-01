# Sprzętowy test utraty zasilania podczas zapisu KV na RP

Stanowisko zapisuje do natywnej pamięci flash RP kontrolowane, niekompletne
banki KV po kasowaniu, zapisie treści i jej weryfikacji. Ponowne wczytanie
fizycznej pamięci do kopii EEPROM modeluje restart. Test sprawdza powrót do
poprzedniego kompletnego banku oraz odzyskanie nowszego banku, gdy błąd został
zgłoszony już po pełnej publikacji. Przełącznika fault injection używanego
wyłącznie podczas budowania stanowiska nie wolno włączać w firmware produkcyjnym.
