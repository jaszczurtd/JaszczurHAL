<a id="jpeg"></a>

# JPEG - dekodowanie obrazów

*Dostępne również [po angielsku](../en/19_JPEG.md).*

> **Część [Dokumentacji API JaszczurHAL](../../pl/JaszczurHAL_API.md)**

Dekodowanie obrazów JPEG do RGB565 przez dołączony `TJpgDec` (`HAL_ENABLE_JPEG`). Flaga `HAL_ENABLE_JPEG_AS_BASE64` dodatkowo udostępnia odczyt JPEG zapisanych jako Base64.

JaszczurHAL kompiluje przenośny rdzeń Tiny JPEG Decompressor napisany w C. Nie dołącza interfejsu Arduino, adapterów systemów plików ani obsługi wyświetlacza z repozytorium źródłowego. Źródła `jaszczurtd/TJpg_Decoder` są pobierane do `third_party/TJpg_Decoder`; dokładny commit określa `third_party/jpeg_version.conf`.

Wersja dostarczana z projektem: `TJpg_Decoder` 1.1.0, w tym TJpgDec R0.03.
Pobrane źródła zawierają warunki licencyjne dekodera ChaN oraz licencję
FreeBSD Bodmera w `third_party/TJpg_Decoder/license.txt` i nagłówkach plików
źródłowych.

<a id="włączanie"></a>

## Włączenie modułu

Włącz moduł w pliku `hal_project_config.h` lub za pomocą definicji
kompilatora:

```c
#pragma once

#define HAL_ENABLE_JPEG
```

Aby korzystać z JPEG zapisanych jako Base64, włącz zamiast tego:

```c
#pragma once

#define HAL_ENABLE_JPEG_AS_BASE64
```

`HAL_ENABLE_JPEG_AS_BASE64` automatycznie włącza `HAL_ENABLE_CRYPTO` i `HAL_ENABLE_JPEG`.

Bez `HAL_ENABLE_JPEG` kod źródłowy rdzenia i warstwa integracyjna przechowywana
w repozytorium tworzą puste jednostki translacji. Ta sama flaga musi być
aktywna podczas kompilowania kodu korzystającego bezpośrednio z rdzenia lub
z funkcji pomocniczych `jpeg*`.

<a id="dołączanie"></a>

## Dołączenie nagłówków

Dla funkcji dekodujących do RGB565 w C lub C++ dołącz:

```c
#include <hal/codecs/hal_image.h>
```

Do bezpośredniego API C dekodera TJpgDec użyj:

```c
#include <hal/codecs/jpeg/tjpgd.h>
```

Nagłówki zgodności zachowują dawne aliasy bez prefiksu. W nowym kodzie używaj nazw `hal_image_*`.

## Konfiguracja dla systemów wbudowanych

Dekoder pobiera skompresowany obraz z pamięci i przekazuje prostokątne fragmenty wyniku przez callbacki TJpgDec. Konfiguracja używana przez JaszczurHAL:

- generuje piksele RGB565;
- używa tymczasowego obszaru roboczego dekodera o rozmiarze 3500 bajtów;
- nie alokuje pamięci wewnętrznie;
- obsługuje dane JPEG typu baseline w skali szarości oraz YCbCr;
- obsługuje próbkowanie 4:4:4, 4:2:0 oraz poziome 4:2:2;
- odrzuca dane JPEG typu progressive;
- pozwala dekodować obraz w skali 1:1, 1:2, 1:4 lub 1:8 przez bezpośrednie API
  TJpgDec.

Funkcje wysokiego poziomu JaszczurHAL dekodują tylko w skali 1:1. Odczyt pliku należy wykonać osobno, przez API pamięci masowej HAL.

<a id="zakres-api"></a>

## Dostępne operacje

| Kategoria | Funkcje |
|---|---|
| Funkcja pomocnicza RGB565 | `hal_image_jpeg_decode_rgb565` |
| Funkcje pomocnicze Base64 | `hal_image_jpeg_base64_decoded_size`, `hal_image_jpeg_base64_decode_rgb565` |
| Bezpośrednie API dekodera | `jd_prepare`, `jd_decomp` |

## Zarządzanie pamięcią

Dla funkcji wysokiego poziomu aplikacja dostarcza bufory wejścia, wyniku oraz - przy Base64 - bufor pośredni. Obszar roboczy samego dekodera jest przydzielany oddzielnie, zgodnie z zasadami poniżej:

- `hal_image_jpeg_decode_rgb565()` odczytuje bajty JPEG z pamięci i zapisuje piksele
  RGB565 do bufora wyjściowego dostarczonego przez wywołującego.
- `hal_image_jpeg_base64_decoded_size()` sprawdza poprawność Base64 i zwraca dokładny rozmiar
  JPEG po dekodowaniu, ale nie zapisuje zdekodowanych danych.
- `hal_image_jpeg_base64_decode_rgb565()` dekoduje Base64 do bufora roboczego JPEG
  dostarczonego przez wywołującego, a następnie dekoduje JPEG do bufora
  wyjściowego RGB565 dostarczonego przez wywołującego.
- Bufor wyjściowy RGB565 musi pomieścić co najmniej `width * height` pikseli.
- Przy każdym wywołaniu funkcji wysokiego poziomu adapter dekodera przydziela,
  a następnie zwalnia obszar roboczy TJpgDec o rozmiarze 3500 bajtów.
- Funkcje pomocnicze zwracają `false`, jeśli argumenty lub dane Base64 są
  nieprawidłowe, JPEG ma nieobsługiwany format, nie uda się przydzielić
  pamięci, dekodowanie zakończy się błędem albo bufor jest za mały.

<a id="przykład-dekodowanie-bajtów-jpeg-do-rgb565"></a>

## Przykład: dekodowanie JPEG do RGB565

```c
#include <hal/codecs/hal_image.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool decode_jpeg_rgb565(const uint8_t *jpeg,
                               size_t jpeg_size,
                               unsigned short *rgb565,
                               size_t rgb565_pixels,
                               unsigned *width,
                               unsigned *height) {
    return hal_image_jpeg_decode_rgb565(jpeg, jpeg_size,
                            rgb565, rgb565_pixels,
                            width, height);
}
```

## Przykład: dekodowanie JPEG zakodowanego w Base64

```c
#include <hal/codecs/hal_image.h>
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
    if (!hal_image_jpeg_base64_decoded_size(jpeg_base64, jpeg_base64_len,
                               &jpeg_work_size) ||
        jpeg_work_size == 0) {
        return false;
    }

    uint8_t *jpeg_work = malloc(jpeg_work_size);
    if (jpeg_work == NULL) {
        return false;
    }

    bool ok = hal_image_jpeg_base64_decode_rgb565(jpeg_base64, jpeg_base64_len,
                                     jpeg_work, jpeg_work_size,
                                     rgb565, rgb565_pixels,
                                     width, height);
    free(jpeg_work);
    return ok;
}
```

<a id="skrypt-zasobów-jpeg-do-base64"></a>

## Przygotowanie zasobu JPEG w Base64

Skrypt `scripts/image_to_base64.py` zamienia plik JPEG na deklarację łańcucha C. Można ją osadzić w oprogramowaniu i odczytać po włączeniu `HAL_ENABLE_JPEG_AS_BASE64`.

Wyświetlenie deklaracji C w konsoli:

```bash
./scripts/image_to_base64.py icon.jpg
```

Domyślne wyjście:

```c
static const char image[] =
    "...base64...";
```

Zapis deklaracji do pliku:

```bash
./scripts/image_to_base64.py icon.jpg --output icon_base64.txt
```

`--otput` jest akceptowane jako alias zachowany dla zgodności. Użyj
`--name`, aby wybrać nazwę zmiennej C:

```bash
./scripts/image_to_base64.py icon.jpg --name kBase64JpegImage
```

## Przykład: Base64 JPEG do ILI9341

Kompletny przykład `examples/07_display_media` pokazuje przygotowanie danych i wyświetlenie obrazu:

1. `hal_image_jpeg_base64_decoded_size()` oblicza dokładny rozmiar JPEG po dekodowaniu
   Base64.
2. Tekst Base64 jest dekodowany do bufora roboczego o dokładnie wyliczonym
   rozmiarze.
3. `hal_image_jpeg_base64_decode_rgb565()` dekoduje obraz JPEG typu baseline bezpośrednio
   do RGB565.
4. Obrazy większe niż `hal_display_get_width()` / `hal_display_get_height()`
   są odrzucane przez przykład przed rysowaniem.
5. `hal_display_draw_rgb_bitmap()` rysuje obraz RGB565 na wyświetlaczu
   ILI9341.

Ten sam projekt sprawdza także bezpośrednie dekodowanie JPEG z pamięci przed wyświetleniem obrazu.
