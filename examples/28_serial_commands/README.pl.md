<a id="28---router-poleceń-portu-szeregowego"></a>

# 28 - Polecenia tekstowe przez port szeregowy

Przykład udostępnia dwa polecenia: `echo` zwraca przesłany tekst, a `info`
podaje dane żądania i czas działania urządzenia. Komunikaty mają format ramek
Serial Session. `hal_serial_commands` odbiera je z domyślnego portu
`hal_serial` i przekazuje do osobnego `hal_command_router`.

Oba polecenia dopuszczają tylko źródło
`HAL_COMMAND_SOURCE_SERIAL_SESSION`. To ograniczenie sposobu dostarczenia
żądania, nie uwierzytelnienie klienta; konfiguracja ustawia
`required_security = 0u`.

Projekt nie wymaga odbiornika GPS ani dodatkowego UART zarezerwowanego przez
aplikację. Na płytkach RP można korzystać z USB CDC, a na pozostałych
platformach - z wybranego portu `hal_serial`.

## Kompilacja

Uruchom z głównego katalogu repozytorium:

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 28_serial_commands
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 28_serial_commands
```

Dostępne są także konfiguracje RP2350 ARM i RISC-V. Aby skompilować
pojedynczą konfigurację, użyj:

```bash
vscode/entry/jh-vscode build \
  --project examples/28_serial_commands --target rp2040 --board pico
```

## Wymiana danych przez port szeregowy

Otwórz port urządzenia i zakończ każde żądanie znakiem nowej linii.
Suma CRC obejmuje bajty między `$` a `*`, zgodnie z `hal_serial_frame.h`.

Najpierw rozpocznij sesję:

```text
$SC,1,HELLO*0F
```

Odpowiedź zachowuje numer sekwencji `1`. Podaje moduł, protokół,
wygenerowany identyfikator sesji, wersję oprogramowania, identyfikator
kompilacji i UID urządzenia.

Wyślij `echo`; druga linia poniżej pokazuje odpowiedź:

```text
$SC,2,echo hello router*5B
$SC,2,hello router*08
```

Aby odczytać dane żądania i czas działania urządzenia, wyślij:

```text
$SC,3,info*74
```

Treść odpowiedzi ma postać:

```text
source=SERIAL_SESSION request=3 session=<id> uptime_ms=<value>
```

Zakończ sesję poleceniem `BYE`; druga linia pokazuje odpowiedź:

```text
$SC,4,BYE*EF
$SC,4,OK BYE*9B
```

Przed `HELLO` żądania otrzymują `ERR HELLO_REQUIRED`. Nieznane polecenie
jest przekazywane do routera, a adapter zwraca `ERR HAL_ENOENT`.
`HELLO` rozpoczyna wymianę zgodną z protokołem, ale samo w sobie nie
potwierdza tożsamości klienta.

## Co pokazuje przykład

Aplikacja tworzy własny router i rejestruje polecenia bez zastępowania
istniejącej nazwy. Router kopiuje ich definicje; obiekty sesji i adaptera
pozostają własnością aplikacji.

Procedury obsługi odczytują metadane niezależne od transportu i zwracają
tekstową odpowiedź z numerem odpowiadającym żądaniu. Po błędzie inicjalizacji
aplikacja najpierw zwalnia adapter, a dopiero potem router, z którego
adapter korzystał.
