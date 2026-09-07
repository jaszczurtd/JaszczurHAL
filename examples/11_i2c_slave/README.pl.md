# 11 - Udostępnianie rejestrów przez I2C

Przykład uruchamia mikrokontroler jako urządzenie podrzędne I2C pod adresem
`0x42`. Zewnętrzny kontroler magistrali może odczytywać udostępnioną mapę
rejestrów. Aplikacja ustawia znacznik stanu `0xA5`, a co sekundę zapisuje
licznik i czas działania oraz wypisuje liczbę transakcji I2C w konsoli.

## Połączenia

| Sygnał | Rodzina RP | STM32G474 |
|---|---|---|
| SDA | GP4 | PB9 |
| SCL | GP5 | PB8 |

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
  --project examples/11_i2c_slave --target rp2040 --board pico
```

Konfiguracja obejmuje również RP2350 ARM, RP2350 RISC-V i STM32G474.
