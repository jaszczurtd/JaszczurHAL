# Integracja LodePNG

Kopia źródeł LodePNG jest utrzymywana w `third_party/lodepng` na dokładnym
commicie zapisanym w `third_party/lodepng_version.conf`. Pliki w tym katalogu
zachowują ścieżkę nagłówka JaszczurHAL i domyślny profil działający wyłącznie w
pamięci. Wrapper zapewnia wiązanie C dla kodu napisanego w C, natomiast
implementacja utrzymywana przez projekt jest kompilowana jako C++. Build dla
RP2350 RISC-V stosuje ustawienie optymalizatora dotyczące wyłącznie tego źródła,
bez modyfikowania kopii utrzymywanej przez projekt.

Do synchronizacji lub weryfikacji tej zależności służy
`scripts/ensure_lodepng.sh`.
