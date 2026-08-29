# Integracja TJpgDec

Checkout `jaszczurtd/TJpg_Decoder` jest zarządzany w
`third_party/TJpg_Decoder` według dokładnego commita z
`third_party/jpeg_version.conf`.

Build obejmuje wyłącznie niezależny od targetu rdzeń Tiny JPEG Decompressor.
Śledzone pliki `tjpgd.c` i `tjpgd.h` włączają go przez `HAL_ENABLE_JPEG`, a
adaptacja wejścia z pamięci i wyjścia RGB565 znajduje się w
`src/utils/tools.cpp`.

Zależność synchronizuje i weryfikuje `scripts/ensure_jpeg.sh`.
