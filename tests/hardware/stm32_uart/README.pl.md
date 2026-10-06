# Test sprzętowego UART na STM32G474

`tests/hardware/stm32_uart` sprawdza `hal_uart` na NUCLEO-G474RE: PORT_2
(USART2 na PA2/PA3), który dochodzi do hosta przez wirtualny port COM
ST-LINK, oraz PORT_1 (USART1) przez samotest w trybie jednoprzewodowym na
PC4, bez żadnego przewodu. Działa na samej Nucleo i z nakładką CAN-FD (profil
`nucleo-g474re-canhat`, drzewo zegarów 160 MHz, sterowanie przekaźników
trzymane w stanie wyłączonym).

Po resecie konsola debug przez trzy sekundy wypisuje linię startową przy
115200: przyczynę resetu, numer seryjny i UID. Potem fixture przejmuje USART2
przy 3 Mbaud 8N1 i odpowiada na komendy liniowe weryfikatora. Wbudowana dioda
miga, gdy linia startowa się powtarza, i przełącza się przy każdej komendzie.

Weryfikator sprawdza:

- numer seryjny (12 bajtów, 24 cyfry szesnastkowe) i UID, także względem słów
  UID odczytanych przez debugger, jeśli zostaną podane;
- echo 64 KiB i 1 MB losowych danych przy 3 Mbaud, z odczytem wielu bajtów i
  nieblokującym zapisem typu wszystko albo nic;
- 200 KB przez `hal_uart_try_write_ex()` i 50 KB przez blokujący
  `hal_uart_write_ex()`, oba z prędkością linii;
- przepełnienie: 2000 bajtów wysłanych, gdy fixture nie czyta; najnowsze 256
  bajtów musi zostać do odczytu w kolejności, a pozostałe 1744 muszą zostać
  policzone;
- błędy ramki (bajty przy 1 Mbaud) i parzystości (nieparzysta parzystość do
  8E1) w licznikach błędów;
- formaty ramek 8E1, 8O1, 8N2, 8E2, 7E1 i 7O1 z echem; 5N1 i 6N1 muszą zostać
  odrzucone, a port ma dalej działać. 7N1, 6E1 i 6O1 też są próbowane, ale
  nie oblewają przebiegu: mostek ST-LINK ich nie przenosi, pokrywa je
  samotest USART1;
- 1 Mbaud, 9600 baud i 1200 baud (preskaler USART) z echem; 20 Mbaud musi
  zostać odrzucone;
- USART1 w trybie jednoprzewodowym (zmiana jednego rejestru w fixture łączy
  TX i RX wewnątrz USART-a; resztę robi HAL): 4 KiB przy 3 Mbaud w 8N1, 8E1,
  8O1, 8E2, 7N1, 7E1, 6E1 i 6O1, przy 1 Mbaud oraz 48 bajtów przy 1200 baud,
  bez błędów;
- wstrzymane przerwania odbioru: fixture maskuje przerwania na 40 ms, a host
  wysyła od 300 do 4000 bajtów. Wrócić muszą wszystkie bajty albo najnowsze
  bajty z resztą policzoną w `rx_buffer_overflow`, albo najnowsze bajty ze
  zdarzeniem `rx_overrun`; maska 2 ms nie może zgłosić zdarzenia;
- wyjście konsoli (`hal_deb`, `printf`) nie może trafić na linię, gdy port
  jest otwarty, i musi wrócić przy 115200 po `hal_uart_destroy()`;
- bez `--skip-reset` (na nakładce każdy reset klika przekaźnikami):
  `hal_system_reset()`, po którym następny start zgłasza `SOFT`, oraz
  przepełnienie stosu głównego aż do osłony stosu przy otwartym porcie: na
  linii nie ma tekstu, następny start zgłasza `STACK_OVERFLOW`.

Wersja z FreeRTOS dodaje test dwóch zadań: jedno nadaje blokującymi
zapisami, a `app_task1` czyta to, co wysyła host, i sprawdza blokadę
piszącego zapisami nieblokującymi o zerowej długości, przy 1 Mbaud i 3 Mbaud.
Oba strumienie muszą dojść bez błędów, próba musi być odrzucana, gdy piszący
trzyma kolejkę, i wracać w ciągu jednego taktu planisty.

Budowanie i wgrywanie przez ST-LINK (OpenOCD); dla Nucleo bez nakładki użyj
`--board nucleo-g474re`:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/stm32_uart \
  --target stm32g474 --board nucleo-g474re-canhat
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_uart \
  --target stm32g474 --board nucleo-g474re-canhat
```

Wersję z FreeRTOS daje wariant `FREERTOS` (`HAL_ENABLE_FREERTOS=1`,
`HAL_ENABLE_APP_TASK1=1`). Dodaj `--variant FREERTOS` do obu poleceń:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_uart \
  --target stm32g474 --board nucleo-g474re-canhat --variant FREERTOS
```

Aby uruchomić profil `nucleo-g474re` (170 MHz) na Nucleo z nakładką, użyj
`--board nucleo-g474re --variant HAT_RELAYS`. Wariant definiuje
`UART_FIXTURE_HAT_RELAYS=1`, więc sterowanie przekaźników pozostaje
wyłączone.

Opcjonalnie odczytaj słowa UID bez resetowania płytki:

```sh
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c init -c "mdw 0x1fff7590 3" -c exit
```

Uruchom weryfikator; podaj trzy słowa jako 24 cyfry szesnastkowe, aby
porównać je ze zgłoszonym numerem seryjnym:

```sh
python3 tests/hardware/stm32_uart/verify_stm32_uart.py \
  --port /dev/serial/by-id/<wirtualny port com st-link> \
  --expect-serial <słowo0><słowo1><słowo2>
```

Weryfikator znajduje fixture w trybie komend albo czeka na jego linię
startową (naciśnij reset, jeśli płytka wystartowała dawno i nie odpowiada).
Wypisuje linię dla każdego sprawdzenia (`INFO` dla sprawdzeń, które nie
oblewają przebiegu) i podsumowanie JSON; `--json <plik>` zapisuje wszystkie
wyniki. Kod wyjścia 0 oznacza sukces, 1 porażkę.
