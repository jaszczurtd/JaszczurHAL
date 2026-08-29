# LodePNG

*Dostępne również [po angielsku](../en/18_LodePNG.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Zakres dokumentu: zarządzany `LodePNG` włączany przez `HAL_ENABLE_PNG` oraz
pomocnicze funkcje Base64 dla PNG włączane przez `HAL_ENABLE_PNG_AS_BASE64`.

`LodePNG` to samodzielny koder/dekoder PNG pobierany do katalogu
`third_party/lodepng` w commicie przypiętym w pliku
`third_party/lodepng_version.conf`. Cienkie wrappery integracyjne w
`src/hal/codecs/lodepng/` dołączają źródło upstream tylko przy `HAL_ENABLE_PNG` i
udostępniają API oparte na pamięci poprzez istniejącą ścieżkę dołączania.

Zarządzana wersja: `LodePNG` 20260119 z forka `jaszczurtd/lodepng`.

Śledzony wrapper zachowuje ABI C, gdy źródło jest kompilowane jako C++. GCC 15
dla RP2350 RISC-V kompiluje to źródło z `-fno-inline`, aby uniknąć fałszywie
pozytywnego wyniku optymalizatora międzyproceduralnego, zachowując przy tym
pełną politykę ostrzeżeń. Zarządzany checkout pozostaje niezmieniony.

Autor/licencja: upstreamowy `LodePNG` jest autorstwa Lode Vandevenne i
rozpowszechniany na licencji zlib.

## Włączanie

Włącz moduł w pliku `hal_project_config.h` lub za pomocą definicji
kompilatora:

```c
#pragma once

#define HAL_ENABLE_PNG
```

Dla zasobów PNG zakodowanych w Base64 włącz zamiast tego flagę pomocniczą:

```c
#pragma once

#define HAL_ENABLE_PNG_AS_BASE64
```

`HAL_ENABLE_PNG_AS_BASE64` propaguje zarówno `HAL_ENABLE_CRYPTO`, jak i
`HAL_ENABLE_PNG`, więc dekoder Base64 i LodePNG są kompilowane razem.

Plik źródłowy jest częścią współdzielonej listy źródeł frameworka, ale jego
zawartość kompiluje się do niczego, dopóki nie zostanie zdefiniowana flaga
`HAL_ENABLE_PNG`. Publiczny nagłówek jest również zabezpieczony, więc kod
korzystający z symboli `lodepng_*` musi być kompilowany z tą samą flagą.

## Dołączanie

Bezpośrednie dołączenie, bezpieczne zarówno z C, jak i C++:

```c
#include <hal/codecs/lodepng/lodepng.h>
```

Dla plików C++ korzystających już z agregatora narzędzi, `tools.h` również
udostępnia LodePNG, gdy zdefiniowano `HAL_ENABLE_PNG`:

```c
#include <tools.h>
```

`tools_c.h` nie eksportuje ponownie samego LodePNG, ale udostępnia pomocnicze
funkcje Base64 PNG JaszczurHAL z `tools_api.h`, gdy zdefiniowano
`HAL_ENABLE_PNG_AS_BASE64`.

## Profil wbudowany

Domyślnie JaszczurHAL zachowuje upstreamowe API C oparte na pamięci i
wyłącza:

- `LODEPNG_COMPILE_DISK` - brak pomocników `FILE` / dysku.
- `LODEPNG_COMPILE_CPP` - brak wrappera `std::vector` / `std::string`.

Jeśli aplikacja rzeczywiście potrzebuje tych opcjonalnych sekcji upstream,
zdefiniuj `HAL_LODEPNG_ENABLE_DISK` lub `HAL_LODEPNG_ENABLE_CPP` przed
dołączeniem `hal/codecs/lodepng/lodepng.h`.

Zwykłe upstreamowe flagi `LODEPNG_NO_COMPILE_*` nadal działają do dalszego
przycinania, na przykład wyłączenia kodera lub dekodera w mocno ograniczonym
buildzie.

## Zakres API

| Kategoria | Funkcje |
|---|---|
| Dekodowanie | `lodepng_decode_memory`, `lodepng_decode32`, `lodepng_decode24` |
| Kodowanie | `lodepng_encode_memory`, `lodepng_encode32`, `lodepng_encode24` |
| Stan zaawansowany | `lodepng_state_init`, `lodepng_state_cleanup`, `lodepng_decode`, `lodepng_encode` |
| Pomocnicy koloru | `lodepng_color_mode_init`, `lodepng_color_mode_cleanup`, `lodepng_get_raw_size` |
| Błędy | `lodepng_error_text` |
| Pomocnicy Base64 | `pngBase64DecodedSize`, `pngBase64Decode32`, `pngBase64DecodeRgb565` |

## Własność pamięci

Proste funkcje kodowania/dekodowania alokują bufory wyjściowe za pomocą
alokatora LodePNG. Przy domyślnym profilu alokatora zwalniaj zwrócone bufory
za pomocą `free(ptr)`.

Najważniejsze reguły:

- `lodepng_decode32()` i `lodepng_decode24()` alokują surowy bufor pikseli.
- `lodepng_encode32()` i `lodepng_encode24()` alokują bufor bajtów PNG.
- `pngBase64DecodedSize()` waliduje Base64 i zwraca dokładną liczbę bajtów
  zdekodowanego PNG, bez zapisywania zdekodowanych bajtów.
- `pngBase64Decode32()` dekoduje Base64 do bufora roboczego PNG dostarczonego
  przez wywołującego, a następnie alokuje wyjście RGBA8888 za pomocą LodePNG.
- `pngBase64DecodeRgb565()` używa tego samego bufora roboczego PNG
  dostarczonego przez wywołującego, alokuje tymczasowy bufor RGBA8888 za
  pomocą LodePNG, konwertuje go do wyjściowego RGB565 dostarczonego przez
  wywołującego, a następnie zwalnia tymczasowy bufor RGBA8888.
- `lodepng_state_init()` musi być sparowane z `lodepng_state_cleanup()`.
- Niestandardowa alokacja może być dostarczona za pomocą upstreamowej flagi
  `LODEPNG_NO_COMPILE_ALLOCATORS` oraz zewnętrznych definicji
  `lodepng_malloc`, `lodepng_realloc`, `lodepng_free`.

## Przykład: Dekodowanie do RGB565

```c
#include <tools_c.h>
#include <hal/codecs/lodepng/lodepng.h>
#include <stdbool.h>
#include <stdlib.h>

static bool decode_icon_rgb565(const unsigned char *png,
                               size_t png_size,
                               unsigned short *rgb565,
                               size_t rgb565_pixels,
                               unsigned *width,
                               unsigned *height) {
    unsigned char *rgba = NULL;
    unsigned error = lodepng_decode32(&rgba, width, height, png, png_size);
    if (error != 0) {
        return false;
    }

    size_t pixels = (size_t)(*width) * (size_t)(*height);
    bool ok = pixels <= rgb565_pixels &&
              rgba8888ToRgb565(rgba, rgb565, pixels);

    free(rgba);
    return ok;
}
```

## Przykład: Dekodowanie PNG zakodowanego w Base64

```c
#include <tools_c.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static bool decode_base64_icon_rgb565(const char *png_base64,
                                      size_t png_base64_len,
                                      unsigned short *rgb565,
                                      size_t rgb565_pixels,
                                      unsigned *width,
                                      unsigned *height) {
    unsigned png_error = 0;
    size_t png_work_size = 0;
    if (!pngBase64DecodedSize(png_base64, png_base64_len, &png_work_size) ||
        png_work_size == 0) {
        return false;
    }

    uint8_t *png_work = malloc(png_work_size);
    if (png_work == NULL) {
        return false;
    }

    bool ok = pngBase64DecodeRgb565(png_base64, png_base64_len,
                                    png_work, png_work_size,
                                    rgb565, rgb565_pixels,
                                    width, height, &png_error);
    free(png_work);
    return ok;
}
```

## Skrypt zasobów: PNG do Base64

Użyj `scripts/image_to_base64.py`, aby przekształcić plik PNG w łańcuch
znaków C, który można osadzić w firmware i zdekodować przy pomocy
`HAL_ENABLE_PNG_AS_BASE64`.

Wypisz wygenerowaną deklarację C na konsolę:

```bash
./scripts/image_to_base64.py icon.png
```

Domyślne wyjście:

```c
static const char image[] =
    "...base64...";
```

Zapisz wygenerowany tekst do pliku:

```bash
./scripts/image_to_base64.py icon.png --output icon_base64.txt
```

`--otput` jest akceptowane jako alias zgodności dla tej samej opcji. Użyj
`--name`, aby wybrać nazwę zmiennej C:

```bash
./scripts/image_to_base64.py icon.png --name kBase64PngImage
```

## Przykład: Base64 PNG do ILI9341

`examples/07_display_media` pokazuje pełną ścieżkę wyświetlania:

1. `pngBase64DecodedSize()` oblicza dokładną liczbę bajtów zdekodowanego PNG.
2. Tekst Base64 jest dekodowany do dokładnie dopasowanego rozmiarem bufora
   roboczego PNG.
3. `lodepng_inspect()` waliduje wymiary obrazu przed pełnym dekodowaniem
   RGBA.
4. Obrazy większe niż `hal_display_get_width()` / `hal_display_get_height()`
   są odrzucane.
5. `lodepng_decode32()` tworzy RGBA8888.
6. `rgba8888ToRgb565()` konwertuje obraz do RGB565.
7. `hal_display_draw_rgb_bitmap()` rysuje obraz na wyświetlaczu ILI9341.
