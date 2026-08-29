# Integracja LodePNG

Checkout upstream LodePNG jest zarządzany w `third_party/lodepng` według
dokładnego commita z `third_party/lodepng_version.conf`. Pliki w tym katalogu
zachowują ścieżkę include JaszczurHAL i domyślny profil operujący wyłącznie na
pamięci. Wrapper udostępnia linkage C, a zarządzana implementacja jest budowana
jako C++. Build RP2350 RISC-V stosuje ustawienie optymalizacji właściwe dla
źródła bez modyfikowania zarządzanego checkoutu.

Zależność synchronizuje i weryfikuje `scripts/ensure_lodepng.sh`.
