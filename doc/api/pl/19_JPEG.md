# JPEG

*Dostępne również [po angielsku](../en/19_JPEG.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Zakres dokumentu: zarządzany `TJpgDec` włączany przez `HAL_ENABLE_JPEG` oraz
pomocnicze funkcje Base64 dla JPEG włączane przez `HAL_ENABLE_JPEG_AS_BASE64`.

Fork `jaszczurtd/TJpg_Decoder` jest pobierany do katalogu
`third_party/TJpg_Decoder` w commicie przypiętym w pliku
`third_party/jpeg_version.conf`. JaszczurHAL kompiluje wyłącznie neutralny
względem targetu rdzeń C Tiny JPEG Decompressor. Wrapper dla
Arduino, adaptery systemu plików oraz fasada wyświetlacza z tego repozytorium
nie wchodzą w skład buildu.

Zarządzana wersja: `TJpg_Decoder` 1.1.0, w tym TJpgDec R0.03. Czysty checkout
zachowuje warunki licencyjne dekodera ChaN oraz licencję FreeBSD Bodmera w
pliku `third_party/TJpg_Decoder/license.txt` oraz w nagłówkach źródłowych.

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

Kod źródłowy rdzenia oraz śledzony wrapper kompilują się do pustych jednostek
translacji, dopóki nie zostanie zdefiniowana flaga `HAL_ENABLE_JPEG`. Kod
korzystający z surowego rdzenia lub symboli pomocniczych `jpeg*` musi być
kompilowany z tą samą flagą.

## Dołączanie

Dla funkcji pomocniczych RGB565 w C lub C++ dołącz:

```c
#include <tools_c.h>
```

Zarządzane API C dla TJpgDec jest dostępne poprzez:

```c
#include <hal/codecs/jpeg/tjpgd.h>
```

`tools.h` udostępnia ten nagłówek również wtedy, gdy zdefiniowano
`HAL_ENABLE_JPEG`.

## Profil wbudowany

JaszczurHAL podaje skompresowane bajty z pamięci i odbiera zdekodowane
prostokąty poprzez API callbacków TJpgDec. Skonfigurowany rdzeń:

- emituje piksele RGB565;
- używa tymczasowego obszaru roboczego dekodera o rozmiarze 3500 bajtów;
- nie alokuje pamięci wewnętrznie;
- obsługuje dane JPEG typu baseline w skali szarości oraz YCbCr;
- obsługuje próbkowanie 4:4:4, 4:2:0 oraz poziome 4:2:2;
- odrzuca dane JPEG typu progressive;
- zapewnia dekodowanie 1:1, 1:2, 1:4 i 1:8 w surowym API TJpgDec.

Funkcje pomocnicze wysokiego poziomu JaszczurHAL obecnie dekodują wyłącznie w
skali 1:1. Wejście z pliku, jeśli jest potrzebne, powinno być zaimplementowane
poprzez API pamięci masowej JaszczurHAL.

## Zakres API

| Kategoria | Funkcje |
|---|---|
| Funkcja pomocnicza RGB565 | `jpegDecodeRgb565` |
| Funkcje pomocnicze Base64 | `jpegBase64DecodedSize`, `jpegBase64DecodeRgb565` |
| Surowy dekoder | `jd_prepare`, `jd_decomp` |

## Własność pamięci

Funkcje pomocnicze wysokiego poziomu korzystają z buforów wejściowych i
wyjściowych dostarczonych przez wywołującego:

- `jpegDecodeRgb565()` odczytuje bajty JPEG z pamięci i zapisuje piksele
  RGB565 do bufora wyjściowego dostarczonego przez wywołującego.
- `jpegBase64DecodedSize()` waliduje Base64 i zwraca dokładną liczbę bajtów
  zdekodowanego JPEG, bez zapisywania zdekodowanych bajtów.
- `jpegBase64DecodeRgb565()` dekoduje Base64 do bufora roboczego JPEG
  dostarczonego przez wywołującego, a następnie dekoduje JPEG do bufora
  wyjściowego RGB565 dostarczonego przez wywołującego.
- Bufor wyjściowy RGB565 musi pomieścić co najmniej `width * height` pikseli.
- Adapter dekodera alokuje i zwalnia swój obszar roboczy TJpgDec o rozmiarze
  3500 bajtów przy każdym dekodowaniu wysokiego poziomu.
- Funkcje pomocnicze zwracają `false` dla nieprawidłowych argumentów,
  nieprawidłowego Base64, nieobsługiwanych danych JPEG, błędu alokacji,
  błędów dekodowania lub zbyt małych buforów.

## Przykład: Dekodowanie bajtów JPEG do RGB565

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

## Przykład: Dekodowanie JPEG zakodowanego w Base64

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
znaków C, który można osadzić w firmware i zdekodować przy pomocy
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

`--otput` jest akceptowane jako alias zgodności dla tej samej opcji. Użyj
`--name`, aby wybrać nazwę zmiennej C:

```bash
./scripts/image_to_base64.py icon.jpg --name kBase64JpegImage
```

## Przykład: Base64 JPEG do ILI9341

`examples/07_display_media` pokazuje pełną ścieżkę wyświetlania:

1. `jpegBase64DecodedSize()` oblicza dokładną liczbę bajtów zdekodowanego
   JPEG.
2. Tekst Base64 jest dekodowany do dokładnie dopasowanego rozmiarem bufora
   roboczego JPEG.
3. `jpegBase64DecodeRgb565()` dekoduje obraz JPEG typu baseline bezpośrednio
   do RGB565.
4. Obrazy większe niż `hal_display_get_width()` / `hal_display_get_height()`
   są odrzucane przez przykład przed rysowaniem.
5. `hal_display_draw_rgb_bitmap()` rysuje obraz RGB565 na wyświetlaczu
   ILI9341.

Ten sam projekt wykorzystuje też bezpośrednią ścieżkę dekodowania wyłącznie z
pamięci przed renderowaniem.
