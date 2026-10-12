# 11 - Udostępnianie rejestrów przez I2C

Przykład uruchamia mikrokontroler jako urządzenie podrzędne I2C pod adresem
`0x42`. Zewnętrzny kontroler magistrali może odczytywać udostępnioną mapę
rejestrów. Aplikacja ustawia znacznik stanu `0xA5`, a co sekundę zapisuje
licznik i czas działania oraz wypisuje liczbę transakcji I2C w konsoli.

## Połączenia

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę i jej piny z wygenerowanego nagłówka. Poniższa tabela powtarza te
połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| SDA | GPIO4 | PB9, pin 5 CN10 / D14 | GPIO4 |
| SCL | GPIO5 | PB8, pin 3 CN10 / D15 | GPIO5 |

Podłącz drugi kontroler I2C, połącz masy i zapewnij podciąganie linii zgodne
z napięciami użytych urządzeń. Obsługę włącza `HAL_ENABLE_I2C_SLAVE`.

## Mapa rejestrów w dostarczonym kodzie

| Stała | Indeks początkowy | Zapisywana wartość |
|---|---|---|
| `REG_STATUS` | `0` | 8-bitowy znacznik `0xA5` |
| `REG_COUNTER` | `1` | 16-bitowy licznik |
| `REG_MILLIS_HI` | `2` | Starsze 16 bitów czasu działania w milisekundach |
| `REG_MILLIS_LO` | `4` | Młodsze 16 bitów czasu działania w milisekundach |

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/11_i2c_slave --assembly pico
```

Pozostałe montaże to `pico2-arm`, `pico2-riscv`, `nucleo` i `esp32s3-zero`.
