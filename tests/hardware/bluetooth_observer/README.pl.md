# Sprzętowy test Bluetooth Observer

Pełne wymagania, procedurę, kryteria akceptacji i zapisane wyniki zawiera
[główny opis stanowisk sprzętowych](../../../doc/api/pl/03_build_tests.md#sprzętowy-test-bluetooth-observer).

Komendy szeregowe `STOP`, `START`, `REOPEN` i `INFO` sprawdzają zatrzymanie i
ponowne uruchomienie skanowania, pełne ponowne uzyskanie profilu BLE bez resetu
kontrolera oraz ograniczoną diagnostykę. Test regresji współistnienia musi
odebrać kolejny poprawny raport zarówno po `START`, jak i po `REOPEN`.
