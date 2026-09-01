# JPEG

*Dostępne również [po angielsku](../en/19_JPEG.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Zakres dokumentu: biblioteka `TJpgDec` dostarczana z JaszczurHAL i włączana
przez `HAL_ENABLE_JPEG` oraz funkcje pomocnicze do obsługi JPEG w formacie
Base64, włączane przez `HAL_ENABLE_JPEG_AS_BASE64`.

Źródła z repozytorium `jaszczurtd/TJpg_Decoder` są pobierane do katalogu
`third_party/TJpg_Decoder` w commicie wskazanym przez
`third_party/jpeg_version.conf`. JaszczurHAL kompiluje wyłącznie napisany w C
rdzeń Tiny JPEG Decompressor, który nie zależy od targetu. Pozostałe elementy
tamtego repozytorium - interfejs Arduino, adaptery systemu plików i warstwa
obsługi wyświetlacza - nie wchodzą w skład buildu.

Wersja dostarczana z projektem: `TJpg_Decoder` 1.1.0, w tym TJpgDec R0.03.
Pobrane źródła zawierają warunki licencyjne dekodera ChaN oraz licencję
FreeBSD Bodmera w `third_party/TJpg_Decoder/license.txt` i nagłówkach plików
źródłowych.

## Włączanie

Włącz moduł w pliku `hal_project_config.h` lub za pomocą definicji
kompilatora:

```c
#pragma once

#define HAL_ENABLE_JPEG
```

Dla zasobów JPEG zakodowanych w Base64 włącz zamiast tego flagę pomocniczą:

```c
#pragma once

#define HAL_ENABLE_JPEG_AS_BASE64
```

`HAL_ENABLE_JPEG_AS_BASE64` propaguje zarówno `HAL_ENABLE_CRYPTO`, jak i
`HAL_ENABLE_JPEG`.

Bez `HAL_ENABLE_JPEG` kod źródłowy rdzenia i warstwa integracyjna przechowywana
w repozytorium tworzą puste jednostki translacji. Ta sama flaga musi być
aktywna podczas kompilowania kodu korzystającego bezpośrednio z rdzenia lub
z funkcji pomocniczych `jpeg*`.

## Dołączanie

Dla funkcji pomocniczych RGB565 w C lub C++ dołącz:

```c
#include <tools_c.h>
```

API C biblioteki TJpgDec dostarczanej z projektem jest dostępne przez:

```c
#include <hal/codecs/jpeg/tjpgd.h>
```

`tools.h` udostępnia ten nagłówek również wtedy, gdy zdefiniowano
`HAL_ENABLE_JPEG`.

## Konfiguracja dla systemów wbudowanych

JaszczurHAL przekazuje do TJpgDec skompresowane dane z pamięci, a prostokątne
fragmenty zdekodowanego obrazu odbiera za pośrednictwem API funkcji zwrotnych
TJpgDec. Tak skonfigurowany rdzeń:

- generuje piksele RGB565;
- używa tymczasowego obszaru roboczego dekodera o rozmiarze 3500 bajtów;
- nie alokuje pamięci wewnętrznie;
- obsługuje dane JPEG typu baseline w skali szarości oraz YCbCr;
- obsługuje próbkowanie 4:4:4, 4:2:0 oraz poziome 4:2:2;
- odrzuca dane JPEG typu progressive;
- pozwala dekodować obraz w skali 1:1, 1:2, 1:4 lub 1:8 przez bezpośrednie API
  TJpgDec.

Funkcje wysokiego poziomu JaszczurHAL dekodują obecnie wyłącznie w skali 1:1.
Jeśli aplikacja musi odczytywać obrazy z plików, powinna użyć API pamięci
masowej JaszczurHAL.

## Zakres API

| Kategoria | Funkcje |
|---|---|
| Funkcja pomocnicza RGB565 | `jpegDecodeRgb565` |
| Funkcje pomocnicze Base64 | `jpegBase64DecodedSize`, `jpegBase64DecodeRgb565` |
| Bezpośrednie API dekodera | `jd_prepare`, `jd_decomp` |

## Zarządzanie pamięcią

Funkcje pomocnicze wysokiego poziomu korzystają z buforów wejściowych i
wyjściowych dostarczonych przez wywołującego:

- `jpegDecodeRgb565()` odczytuje bajty JPEG z pamięci i zapisuje piksele
  RGB565 do bufora wyjściowego dostarczonego przez wywołującego.
- `jpegBase64DecodedSize()` sprawdza poprawność Base64 i zwraca dokładny rozmiar
  JPEG po dekodowaniu, ale nie zapisuje zdekodowanych danych.
- `jpegBase64DecodeRgb565()` dekoduje Base64 do bufora roboczego JPEG
  dostarczonego przez wywołującego, a następnie dekoduje JPEG do bufora
  wyjściowego RGB565 dostarczonego przez wywołującego.
- Bufor wyjściowy RGB565 musi pomieścić co najmniej `width * height` pikseli.
- Przy każdym wywołaniu funkcji wysokiego poziomu adapter dekodera przydziela,
  a następnie zwalnia obszar roboczy TJpgDec o rozmiarze 3500 bajtów.
- Funkcje pomocnicze zwracają `false`, jeśli argumenty lub dane Base64 są
  nieprawidłowe, JPEG ma nieobsługiwany format, nie uda się przydzielić
  pamięci, dekodowanie zakończy się błędem albo bufor jest za mały.

## Przykład: dekodowanie bajtów JPEG do RGB565

```c
#include <tools_c.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool decode_jpeg_rgb565(const uint8_t *jpeg,
                               size_t jpeg_size,
                               unsigned short *rgb565,
                               size_t rgb565_pixels,
                               unsigned *width,
                               unsigned *height) {
    return jpegDecodeRgb565(jpeg, jpeg_size,
                            rgb565, rgb565_pixels,
                            width, height);
}
```

## Przykład: dekodowanie JPEG zakodowanego w Base64

```c
#include <tools_c.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static bool decode_base64_jpeg_rgb565(const char *jpeg_base64,
                                      size_t jpeg_base64_len,
                                      unsigned short *rgb565,
                                      size_t rgb565_pixels,
                                      unsigned *width,
                                      unsigned *height) {
    size_t jpeg_work_size = 0;
    if (!jpegBase64DecodedSize(jpeg_base64, jpeg_base64_len,
                               &jpeg_work_size) ||
        jpeg_work_size == 0) {
        return false;
    }

    uint8_t *jpeg_work = malloc(jpeg_work_size);
    if (jpeg_work == NULL) {
        return false;
    }

    bool ok = jpegBase64DecodeRgb565(jpeg_base64, jpeg_base64_len,
                                     jpeg_work, jpeg_work_size,
                                     rgb565, rgb565_pixels,
                                     width, height);
    free(jpeg_work);
    return ok;
}
```

## Skrypt zasobów: JPEG do Base64

Użyj `scripts/image_to_base64.py`, aby przekształcić plik JPEG w łańcuch
znaków C, który można osadzić w firmware i zdekodować przy włączonym
`HAL_ENABLE_JPEG_AS_BASE64`.

Wypisz wygenerowaną deklarację C na konsolę:

```bash
./scripts/image_to_base64.py icon.jpg
```

Domyślne wyjście:

```c
static const char image[] =
    "...base64...";
```

Zapisz wygenerowany tekst do pliku:

```bash
./scripts/image_to_base64.py icon.jpg --output icon_base64.txt
```

`--otput` jest akceptowane jako alias zachowany dla zgodności. Użyj
`--name`, aby wybrać nazwę zmiennej C:

```bash
./scripts/image_to_base64.py icon.jpg --name kBase64JpegImage
```

## Przykład: Base64 JPEG do ILI9341

`examples/07_display_media` pokazuje cały proces wyświetlania obrazu:

1. `jpegBase64DecodedSize()` oblicza dokładny rozmiar JPEG po dekodowaniu
   Base64.
2. Tekst Base64 jest dekodowany do bufora roboczego o dokładnie wyliczonym
   rozmiarze.
3. `jpegBase64DecodeRgb565()` dekoduje obraz JPEG typu baseline bezpośrednio
   do RGB565.
4. Obrazy większe niż `hal_display_get_width()` / `hal_display_get_height()`
   są odrzucane przez przykład przed rysowaniem.
5. `hal_display_draw_rgb_bitmap()` rysuje obraz RGB565 na wyświetlaczu
   ILI9341.

Ten sam projekt sprawdza również wariant, w którym przed wyświetleniem obraz
jest dekodowany bezpośrednio z pamięci.
