<a id="lodepng"></a>

# PNG - kodowanie i dekodowanie obrazów

*Dostępne również [po angielsku](../en/18_LodePNG.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Kodowanie i dekodowanie obrazów PNG przez dołączoną bibliotekę `LodePNG` (`HAL_ENABLE_PNG`). Flaga `HAL_ENABLE_PNG_AS_BASE64` dodatkowo udostępnia odczyt PNG zakodowanych jako Base64.

Domyślnie API przetwarza obrazy w pamięci, bez operacji plikowych. Źródła LodePNG są pobierane do `third_party/lodepng`; dokładny commit określa `third_party/lodepng_version.conf`. Integracja w `src/hal/codecs/lodepng/` kompiluje je przy włączonym `HAL_ENABLE_PNG` i zachowuje dotychczasową publiczną ścieżkę nagłówka.

Wersja dostarczana z projektem: `LodePNG` 20260119 z repozytorium
`jaszczurtd/lodepng`.

Warstwa integracyjna przechowywana w repozytorium zachowuje ABI C również
wtedy, gdy źródło jest kompilowane jako C++. W przypadku RP2350 RISC-V
kompilator GCC 15 używa dla tego pliku opcji `-fno-inline`. Pozwala to uniknąć
fałszywego ostrzeżenia optymalizatora międzyproceduralnego bez wyłączania
pozostałych ostrzeżeń. Pobrane źródła biblioteki pozostają niezmienione.

Autor/licencja: autorem projektu upstream `LodePNG` jest Lode Vandevenne.
Biblioteka jest udostępniana na licencji zlib.

<a id="włączanie"></a>

## Włączenie modułu

Włącz moduł w pliku `hal_project_config.h` lub za pomocą definicji
kompilatora:

```c
#pragma once

#define HAL_ENABLE_PNG
```

Aby korzystać z zasobów PNG zapisanych jako Base64, włącz zamiast tego:

```c
#pragma once

#define HAL_ENABLE_PNG_AS_BASE64
```

`HAL_ENABLE_PNG_AS_BASE64` automatycznie włącza `HAL_ENABLE_CRYPTO` i `HAL_ENABLE_PNG`, aby razem skompilować dekoder Base64 i obsługę PNG.

Plik należy do wspólnej listy źródeł frameworka, lecz bez `HAL_ENABLE_PNG`
powstaje z niego pusta jednostka translacji. Publiczny nagłówek jest
zabezpieczony tą samą flagą, dlatego musi być ona aktywna także podczas
kompilowania kodu korzystającego z symboli `lodepng_*`.

<a id="dołączanie"></a>

## Dołączenie nagłówków

Nagłówki można dołączać bezpośrednio w C i C++:

```c
#include <hal/codecs/hal_image.h>          // adaptery pamięci/Base64 HAL
#include <hal/codecs/lodepng/lodepng.h>
```

Nagłówki zgodności zachowują dawne aliasy bez prefiksu. W nowym kodzie używaj nazw `hal_image_*`.

## Konfiguracja dla systemów wbudowanych

Domyślna konfiguracja zachowuje oryginalne API C przetwarzające dane w pamięci, natomiast wyłącza:

- `LODEPNG_COMPILE_DISK` - bez funkcji obsługujących `FILE` i pliki na dysku.
- `LODEPNG_COMPILE_CPP` - bez interfejsu C++ opartego na `std::vector` i `std::string`.

Aby włączyć opcjonalną obsługę plików lub interfejs C++, zdefiniuj odpowiednio `HAL_LODEPNG_ENABLE_DISK` lub `HAL_LODEPNG_ENABLE_CPP` przed dołączeniem `hal/codecs/lodepng/lodepng.h`.

Flagi LodePNG `LODEPNG_NO_COMPILE_*` pozwalają ograniczyć rozmiar kodu, na przykład przez wyłączenie nieużywanego kodera albo dekodera.

<a id="zakres-api"></a>

## Dostępne operacje

| Kategoria | Funkcje |
|---|---|
| Dekodowanie | `lodepng_decode_memory`, `lodepng_decode32`, `lodepng_decode24` |
| Kodowanie | `lodepng_encode_memory`, `lodepng_encode32`, `lodepng_encode24` |
| Rozszerzona konfiguracja | `lodepng_state_init`, `lodepng_state_cleanup`, `lodepng_decode`, `lodepng_encode` |
| Obsługa formatów koloru | `lodepng_color_mode_init`, `lodepng_color_mode_cleanup`, `lodepng_get_raw_size` |
| Błędy | `lodepng_error_text` |
| Obsługa Base64 | `hal_image_png_base64_decoded_size`, `hal_image_png_base64_decode_rgba8888`, `hal_image_png_base64_decode_rgb565` |

## Zarządzanie pamięcią

Proste funkcje kodowania i dekodowania same przydzielają bufory wyjściowe przez alokator LodePNG. Przy konfiguracji domyślnej zwalniaj te bufory przez `free(ptr)`.

Zasady korzystania z buforów:

- `lodepng_decode32()` i `lodepng_decode24()` przydzielają bufor samych pikseli.
- `lodepng_encode32()` i `lodepng_encode24()` alokują bufor bajtów PNG.
- `hal_image_png_base64_decoded_size()` sprawdza poprawność Base64 i zwraca dokładny rozmiar
  PNG po dekodowaniu, ale nie zapisuje zdekodowanych danych.
- `hal_image_png_base64_decode_rgba8888()` dekoduje Base64 do bufora roboczego PNG dostarczonego
  przez wywołującego, a następnie przez LodePNG przydziela bufor wyjściowy
  RGBA8888.
- `hal_image_png_base64_decode_rgb565()` używa tego samego bufora roboczego PNG. Za pomocą
  LodePNG przydziela tymczasowy bufor RGBA8888, konwertuje obraz do bufora
  wyjściowego RGB565 dostarczonego przez wywołującego, po czym zwalnia bufor
  tymczasowy.
- Każdemu wywołaniu `lodepng_state_init()` musi odpowiadać
  `lodepng_state_cleanup()`.
- Własny alokator można zastosować przez ustawienie flagi LodePNG
  `LODEPNG_NO_COMPILE_ALLOCATORS` i dostarczenie definicji funkcji
  `lodepng_malloc`, `lodepng_realloc` oraz `lodepng_free`.

## Przykład: dekodowanie do RGB565

```c
#include <hal/codecs/hal_image.h>
#include <hal/codecs/lodepng/lodepng.h>
#include <hal/display/hal_pixel.h>
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
              hal_pixel_rgba8888_buffer_to_rgb565_ex(
                  rgba, rgb565, pixels) == HAL_OK;

    free(rgba);
    return ok;
}
```

## Przykład: dekodowanie PNG zakodowanego w Base64

```c
#include <hal/codecs/hal_image.h>
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
    if (!hal_image_png_base64_decoded_size(png_base64, png_base64_len, &png_work_size) ||
        png_work_size == 0) {
        return false;
    }

    uint8_t *png_work = malloc(png_work_size);
    if (png_work == NULL) {
        return false;
    }

    bool ok = hal_image_png_base64_decode_rgb565(png_base64, png_base64_len,
                                    png_work, png_work_size,
                                    rgb565, rgb565_pixels,
                                    width, height, &png_error);
    free(png_work);
    return ok;
}
```

<a id="skrypt-zasobów-png-do-base64"></a>

## Przygotowanie zasobu PNG w Base64

Skrypt `scripts/image_to_base64.py` zamienia plik PNG na deklarację łańcucha C. Można ją osadzić w oprogramowaniu i odczytać po włączeniu `HAL_ENABLE_PNG_AS_BASE64`.

Wyświetlenie deklaracji C w konsoli:

```bash
./scripts/image_to_base64.py icon.png
```

Domyślne wyjście:

```c
static const char image[] =
    "...base64...";
```

Zapis deklaracji do pliku:

```bash
./scripts/image_to_base64.py icon.png --output icon_base64.txt
```

`--otput` jest akceptowane jako alias zachowany dla zgodności. Użyj
`--name`, aby wybrać nazwę zmiennej C:

```bash
./scripts/image_to_base64.py icon.png --name kBase64PngImage
```

## Przykład: Base64 PNG do ILI9341

Kompletny przykład `examples/07_display_media` pokazuje przygotowanie danych i wyświetlenie obrazu:

1. `hal_image_png_base64_decoded_size()` oblicza dokładny rozmiar PNG po dekodowaniu Base64.
2. Tekst Base64 jest dekodowany do bufora roboczego o dokładnie wyliczonym
   rozmiarze.
3. `lodepng_inspect()` sprawdza wymiary obrazu przed pełnym dekodowaniem
   RGBA.
4. Obrazy większe niż `hal_display_get_width()` / `hal_display_get_height()`
   są odrzucane.
5. `lodepng_decode32()` dekoduje obraz do RGBA8888.
6. `hal_pixel_rgba8888_buffer_to_rgb565_ex()` konwertuje obraz do RGB565.
7. `hal_display_draw_rgb_bitmap()` rysuje obraz na wyświetlaczu ILI9341.
