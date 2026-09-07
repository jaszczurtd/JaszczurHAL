# 02 - Wywołanie funkcji skrótu i szyfrowania

Przykład oblicza podsumę MD5 tekstu `hello`, a następnie szyfruje i odszyfrowuje
bufor z tym tekstem za pomocą ChaCha20-Poly1305. Pokazuje podstawowe wywołania
API z `hal/security/hal_crypto.h`; obsługę włącza `HAL_ENABLE_CRYPTO`.

Obliczenia są wykonywane raz, w `app_start()`.
Przykład ten nie jest testem poprawności kryptografii. Przy obliczaniu
MD5 pomija końcowy bajt NULL, a przy szyfrowaniu uwzględnia cały bufor wraz
z tym bajtem.

**Klucz i nonce są wypełnione zerami i służą wyłącznie demonstracji API.**
Nie używaj ich do ochrony rzeczywistych danych ani jako wzorca zarządzania
kluczami i nonce w aplikacji.

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/02_crypto --target rp2040 --board pico
```

Konfiguracja projektu obejmuje także RP2350 ARM, RP2350 RISC-V i STM32G474.
Przykład nie wymaga dodatkowego sprzętu.
