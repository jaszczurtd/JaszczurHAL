# Integracja TJpgDec

Kopia źródeł `jaszczurtd/TJpg_Decoder` jest utrzymywana w
`third_party/TJpg_Decoder` na dokładnym commicie zapisanym w
`third_party/jpeg_version.conf`.

Kompilacja obejmuje wyłącznie rdzeń Tiny JPEG Decompressor, który jest
niezależny od targetu.
Pliki `tjpgd.c` i `tjpgd.h` przechowywane w repozytorium włączają go przez
`HAL_ENABLE_JPEG`, a
adaptacja wejścia z pamięci i wyjścia RGB565 znajduje się w
`src/utils/tools.cpp`.

Do synchronizacji lub weryfikacji tej zależności służy
`scripts/ensure_jpeg.sh`.
