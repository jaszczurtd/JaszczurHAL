# 28 - Router poleceń serial

Minimalna aplikacja ramkowanej Serial Session używająca niezależnego
`hal_command_router`. Rejestruje `echo` i `info`, ogranicza obie trasy do
`HAL_COMMAND_SOURCE_SERIAL_SESSION` i dołącza `hal_serial_commands` do
domyślnego endpointu serial targetu.

Projekt jest oddzielony od przykładu GPS/UART, ponieważ nie wymaga zewnętrznego
odbiornika ani UART należącego do aplikacji. Na boardach RP można go obsłużyć
przez USB CDC; pozostałe targety używają wybranego endpointu `hal_serial`.

## Build

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 28_serial_commands
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 28_serial_commands
```

Projekt obsługuje też generowane konfiguracje RP2350 ARM i RISC-V. Pojedynczą
konfigurację zbudujesz przez entrypoint VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/28_serial_commands --target rp2040 --board pico
```

## Wymiana serial

Otwórz endpoint serial targetu i wysyłaj każde żądanie zakończone znakiem nowej
linii. CRC obejmuje bajty między `$` i `*`, zgodnie z `hal_serial_frame.h`.

Najpierw aktywuj sesję:

```text
$SC,1,HELLO*0F
```

Odpowiedź zawiera moduł, protokół, wygenerowany identyfikator sesji, wersję
firmware, identyfikator builda i UID urządzenia. Zachowuje numer sekwencji `1`.

Przekaż echo przez router:

```text
$SC,2,echo hello router*5B
$SC,2,hello router*08
```

Odczytaj metadane żądania i uptime targetu:

```text
$SC,3,info*74
```

Dynamiczna odpowiedź ma następujący payload:

```text
source=SERIAL_SESSION request=3 session=<id> uptime_ms=<value>
```

Zakończ sesję:

```text
$SC,4,BYE*EF
$SC,4,OK BYE*9B
```

Żądania wysłane przed `HELLO` otrzymują `ERR HELLO_REQUIRED`. Nieznane nazwy
tras docierają do routera i są zwracane przez adapter jako `ERR HAL_ENOENT`.

## Co pokazuje przykład

- tworzenie i zachowywanie niezależnego routera;
- rejestrowanie kopiowanych definicji tras bez zastępowania istniejącej nazwy;
- ograniczanie handlerów do żądań Serial Session;
- dołączanie stanu sesji i adaptera należącego do wywołującego;
- zwracanie treści tekstowych z zachowaniem sekwencji żądania;
- odczyt niezależnych od transportu metadanych żądania wewnątrz handlera;
- zwalnianie adaptera przed zniszczeniem routera podczas rollbacku startu.
