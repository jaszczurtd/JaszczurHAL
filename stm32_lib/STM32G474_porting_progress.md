# STM32G474 Porting Progress

Data aktualizacji: 2026-04-24

## Cel
Przygotować szkielet nowego targetu `STM32G474` dla JaszczurHAL bez zależności od warstwy Arduino, tak aby można było rozwijać backend STM32 równolegle do istniejącego backendu Arduino/RP2040.

## Zrealizowany zakres ("2)" - szkielet)

### 1. Nowa ścieżka budowania biblioteki statycznej dla STM32
Dodane pliki:
- `stm32_lib/CMakeLists.txt`
- `stm32_lib/toolchain_stm32g474.cmake`
- `build_stm32_lib.sh`

Co zapewniają:
- osobny target CMake `JaszczurHAL` dla STM32G474,
- dedykowany toolchain dla `arm-none-eabi-*`,
- wygodny skrypt budowania analogiczny do istniejącego `build_arduino_lib.sh`.

### 2. Nowy backend źródłowy `impl/stm32g474`
Dodane pliki:
- `src/hal/impl/stm32g474/hal_sync.cpp`
- `src/hal/impl/stm32g474/hal_system.cpp`
- `src/hal/impl/stm32g474/hal_gpio.cpp`
- `src/hal/impl/stm32g474/hal_adc.cpp`
- `src/hal/impl/stm32g474/hal_pwm.cpp`
- `src/hal/impl/stm32g474/hal_timer.cpp`
- `src/hal/impl/stm32g474/hal_serial.cpp`
- `src/hal/impl/stm32g474/hal_spi.cpp`
- `src/hal/impl/stm32g474/hal_i2c.cpp`
- `src/hal/impl/stm32g474/hal_uart.cpp`
- `src/hal/impl/stm32g474/hal_time.cpp` (zawsze dostępny `hal_time_from_components`)

Charakter implementacji:
- minimalny, bezpieczny szkielet pod dalsze przepięcie na STM32 HAL/LL,
- brak zależności od `Arduino.h` i bibliotek Arduino,
- moduły krytyczne mają działające API i stan wewnętrzny (placeholder behavior),
- w kodzie są oznaczenia TODO tam, gdzie docelowo powinno wejść spięcie z STM32.

### 3. Domyślny profil funkcjonalny STM32 (na start)
W `stm32_lib/CMakeLists.txt` domyślnie ustawiono:
- `HAL_DISABLE_WIFI`
- `HAL_DISABLE_TIME`
- `HAL_DISABLE_EEPROM`
- `HAL_DISABLE_GPS`
- `HAL_DISABLE_THERMOCOUPLE`
- `HAL_DISABLE_SWSERIAL`
- `HAL_DISABLE_I2C_SLAVE`
- `HAL_DISABLE_EXTERNAL_ADC`
- `HAL_DISABLE_PWM_FREQ`
- `HAL_DISABLE_RGB_LED`
- `HAL_DISABLE_CAN`
- `HAL_DISABLE_DISPLAY`
- `HAL_DISABLE_UNITY`

To ogranicza zakres do "rdzenia" backendu i upraszcza pierwsze etapy portu.

## Walidacja

### OK
- Standardowy build/test repo pozostał zielony:
  - `cmake -S . -B build`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
- Build nowego `stm32_lib` na kompilatorze hostowym (sanity-check składni i zależności) przeszedł:
  - `cmake -S stm32_lib -B build_stm32_host`
  - `cmake --build build_stm32_host`

### Ograniczenie środowiska lokalnego
- `./build_stm32_lib.sh --clean` zatrzymuje się na braku toolchaina Arm:
  - brak `arm-none-eabi-gcc`
  - brak `arm-none-eabi-g++`

## Jak budować docelowo dla STM32G474
Po zainstalowaniu toolchaina Arm:

```bash
./build_stm32_lib.sh --clean
```

Opcjonalnie:

```bash
./build_stm32_lib.sh --clean \
  -p /sciezka/do/projektu \
  -D HAL_DISABLE_ASSERTS
```

## Co zostało do kolejnych etapów
1. Podmiana placeholderów w `impl/stm32g474` na właściwe wywołania STM32 HAL/LL.
2. Realna implementacja `hal_timer_*` (sprzętowe timery/IRQ).
3. Integracja `hal_system` (watchdog, UID MCU, bootloader, czas).
4. Dodanie smoke-testów sprzętowych (GPIO/UART/I2C/SPI) na płytce STM32G474.
5. Stopniowe odblokowywanie kolejnych modułów (`HAL_DISABLE_*`) w miarę portowania.
