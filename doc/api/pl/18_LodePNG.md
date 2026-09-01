# LodePNG

*Dostępne również [po angielsku](../en/18_LodePNG.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Zakres dokumentu: biblioteka `LodePNG` dostarczana z JaszczurHAL i włączana
przez `HAL_ENABLE_PNG` oraz funkcje pomocnicze do obsługi PNG w formacie Base64,
włączane przez `HAL_ENABLE_PNG_AS_BASE64`.

`LodePNG` to samodzielna biblioteka do kodowania i dekodowania PNG. Jej źródła
są pobierane do `third_party/lodepng` w commicie wskazanym przez
`third_party/lodepng_version.conf`. Warstwa integracyjna w
`src/hal/codecs/lodepng/` kompiluje źródła upstreamu tylko przy włączonym
`HAL_ENABLE_PNG` i udostępnia API operujące na pamięci pod dotychczasową
ścieżką nagłówka.

Wersja dostarczana z projektem: `LodePNG` 20260119 z repozytorium
`jaszczurtd/lodepng`.

Warstwa integracyjna przechowywana w repozytorium zachowuje ABI C również
wtedy, gdy źródło jest kompilowane jako C++. W przypadku RP2350 RISC-V
kompilator GCC 15 używa dla tego pliku opcji `-fno-inline`. Pozwala to uniknąć
fałszywego ostrzeżenia optymalizatora międzyproceduralnego bez wyłączania
pozostałych ostrzeżeń. Pobrane źródła biblioteki pozostają niezmienione.

Autor/licencja: autorem projektu upstream `LodePNG` jest Lode Vandevenne.
Biblioteka jest udostępniana na licencji zlib.

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

Plik należy do wspólnej listy źródeł frameworka, lecz bez `HAL_ENABLE_PNG`
powstaje z niego pusta jednostka translacji. Publiczny nagłówek jest
zabezpieczony tą samą flagą, dlatego musi być ona aktywna także podczas
kompilowania kodu korzystającego z symboli `lodepng_*`.

## Dołączanie

Bezpośrednie dołączenie, bezpieczne zarówno z C, jak i C++:

```c
#include <hal/codecs/lodepng/lodepng.h>
```

Jeżeli plik C++ korzysta już ze zbiorczego nagłówka narzędziowego, może uzyskać
dostęp do LodePNG przez `tools.h`. Wymaga to `HAL_ENABLE_PNG`:

```c
#include <tools.h>
```

`tools_c.h` nie udostępnia bezpośrednio API LodePNG. Przy włączonym
`HAL_ENABLE_PNG_AS_BASE64` dołącza natomiast z `tools_api.h` funkcje pomocnicze
JaszczurHAL obsługujące PNG zakodowane w Base64.

## Konfiguracja dla systemów wbudowanych

Domyślnie JaszczurHAL zachowuje API C upstreamu operujące na pamięci i
wyłącza:

- `LODEPNG_COMPILE_DISK` - bez funkcji obsługujących `FILE` i pliki na dysku.
- `LODEPNG_COMPILE_CPP` - bez interfejsu C++ opartego na `std::vector` i `std::string`.

Jeśli aplikacja rzeczywiście potrzebuje tych opcjonalnych sekcji biblioteki,
zdefiniuj `HAL_LODEPNG_ENABLE_DISK` lub `HAL_LODEPNG_ENABLE_CPP` przed
dołączeniem `hal/codecs/lodepng/lodepng.h`.

Standardowe flagi LodePNG `LODEPNG_NO_COMPILE_*` pozwalają dalej ograniczać
zakres kompilowanego kodu, na przykład wyłączyć koder lub dekoder w buildzie
przeznaczonym dla urządzenia z bardzo ograniczoną pamięcią.

## Zakres API

| Kategoria | Funkcje |
|---|---|
| Dekodowanie | `lodepng_decode_memory`, `lodepng_decode32`, `lodepng_decode24` |
| Kodowanie | `lodepng_encode_memory`, `lodepng_encode32`, `lodepng_encode24` |
| Rozszerzona konfiguracja | `lodepng_state_init`, `lodepng_state_cleanup`, `lodepng_decode`, `lodepng_encode` |
| Obsługa formatów koloru | `lodepng_color_mode_init`, `lodepng_color_mode_cleanup`, `lodepng_get_raw_size` |
| Błędy | `lodepng_error_text` |
| Obsługa Base64 | `pngBase64DecodedSize`, `pngBase64Decode32`, `pngBase64DecodeRgb565` |

## Zarządzanie pamięcią

Proste funkcje kodowania i dekodowania przydzielają bufory wyjściowe przez
alokator LodePNG. Przy domyślnej konfiguracji zwalniaj je za pomocą
`free(ptr)`.

Najważniejsze reguły:

- `lodepng_decode32()` i `lodepng_decode24()` przydzielają bufor samych pikseli.
- `lodepng_encode32()` i `lodepng_encode24()` alokują bufor bajtów PNG.
- `pngBase64DecodedSize()` sprawdza poprawność Base64 i zwraca dokładny rozmiar
  PNG po dekodowaniu, ale nie zapisuje zdekodowanych danych.
- `pngBase64Decode32()` dekoduje Base64 do bufora roboczego PNG dostarczonego
  przez wywołującego, a następnie przez LodePNG przydziela bufor wyjściowy
  RGBA8888.
- `pngBase64DecodeRgb565()` używa tego samego bufora roboczego PNG. Za pomocą
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

## Przykład: dekodowanie PNG zakodowanego w Base64

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
znaków C, który można osadzić w firmware i zdekodować przy włączonym
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

`--otput` jest akceptowane jako alias zachowany dla zgodności. Użyj
`--name`, aby wybrać nazwę zmiennej C:

```bash
./scripts/image_to_base64.py icon.png --name kBase64PngImage
```

## Przykład: Base64 PNG do ILI9341

`examples/07_display_media` pokazuje cały proces wyświetlania obrazu:

1. `pngBase64DecodedSize()` oblicza dokładny rozmiar PNG po dekodowaniu Base64.
2. Tekst Base64 jest dekodowany do bufora roboczego o dokładnie wyliczonym
   rozmiarze.
3. `lodepng_inspect()` sprawdza wymiary obrazu przed pełnym dekodowaniem
   RGBA.
4. Obrazy większe niż `hal_display_get_width()` / `hal_display_get_height()`
   są odrzucane.
5. `lodepng_decode32()` dekoduje obraz do RGBA8888.
6. `rgba8888ToRgb565()` konwertuje obraz do RGB565.
7. `hal_display_draw_rgb_bitmap()` rysuje obraz na wyświetlaczu ILI9341.
