# Ślad HCI Bluetooth Classic

Pełna procedura i zapisana diagnoza znajdują się w
[głównym opisie stanowisk sprzętowych](../../../doc/api/pl/03_build_tests.md#diagnostyka-surowego-inquiry-hci-bluetooth-classic).

Ten prywatny fixture sprzętowy zapisuje surowe pakiety komend i zdarzeń HCI
BTstack przed ich interpretacją przez publiczny manager Classic. Obsługuje Pico
W RP2040 i Pico 2 W RP2350 ARM, dzięki czemu obie płytki uruchamiają tę samą
aplikację. Konsola maskuje adresy Bluetooth i treść zdarzeń niezwiązanych z
inquiry.

Zbuduj i wgraj wybrany target, otwórz konsolę szeregową, wykonaj `SCAN`, poczekaj
na zakończenie dziesięciosekundowego inquiry, a następnie wykonaj `INFO` i
`DUMP`. Polecenie `SCAN30` uruchamia trzy kolejne cykle inquiry. Oba polecenia
skanowania najpierw zerują ślad. `RESET` usuwa ślad bez uruchamiania inquiry,
a `STOP` przerywa aktywny skan. `INFO` podaje liczniki transportu HCI oraz
zmierzony zegar gSPI CYW43.

Rekordy `JHHCI` zachowują surowe bajty komendy Inquiry oraz zdarzeń Inquiry
Complete, Inquiry Result i Inquiry Result with RSSI. Dla Extended Inquiry Result
zachowują wyłącznie metadane do pola RSSI; treść EIR jest ukrywana, ponieważ
może zawierać nazwy i dowolne dane producenta. Adresy Bluetooth są zawsze
maskowane. `JHHCI-PEER` podaje długość reklamowanego pola nazwy, długość tekstu
do pierwszego NUL i skrót FNV-1a, co pozwala diagnozować padding i tożsamość bez
drukowania nazwy. Pozostałe komendy i zdarzenia HCI zachowują tylko bezpieczny
nagłówek potrzebny do porównania sekwencji.
