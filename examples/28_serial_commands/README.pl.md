# 28 - Router poleceń portu szeregowego

Jest to minimalna aplikacja ramkowanej Serial Session, która używa niezależnego
`hal_command_router`. Rejestruje `echo` i `info`, ogranicza obie trasy do
`HAL_COMMAND_SOURCE_SERIAL_SESSION` oraz dołącza `hal_serial_commands` do
domyślnego punktu końcowego portu szeregowego danego targetu.

Projekt jest oddzielony od przykładu GPS/UART, ponieważ nie wymaga zewnętrznego
odbiornika ani portu UART zarezerwowanego przez aplikację. Na płytkach RP można
go obsłużyć przez USB CDC; pozostałe targety używają wybranego punktu końcowego
`hal_serial`.

## Kompilacja

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 28_serial_commands
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 28_serial_commands
```

Projekt obsługuje też generowane konfiguracje RP2350 ARM i RISC-V. Pojedynczą
konfigurację zbudujesz przez punkt wejścia VS Code:

```bash
vscode/entry/jh-vscode build \
  --project examples/28_serial_commands --target rp2040 --board pico
```

## Wymiana danych przez port szeregowy

Otwórz punkt końcowy portu szeregowego targetu i wysyłaj każde żądanie
zakończone znakiem nowej linii. CRC obejmuje bajty między `$` i `*`, zgodnie z
`hal_serial_frame.h`.

Najpierw aktywuj sesję:

```text
$SC,1,HELLO*0F
```

Odpowiedź zawiera moduł, protokół, wygenerowany identyfikator sesji, wersję
firmware, identyfikator kompilacji i UID urządzenia. Zachowuje numer sekwencji `1`.

Przekaż echo przez router:

```text
$SC,2,echo hello router*5B
$SC,2,hello router*08
```

Odczytaj metadane żądania i czas działania targetu:

```text
$SC,3,info*74
```

Dynamiczna odpowiedź zawiera następujące dane:

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

- tworzenie i utrzymywanie niezależnego routera;
- rejestrowanie definicji tras kopiowanych przez router bez zastępowania wpisu
  o istniejącej nazwie;
- ograniczanie procedur obsługi do żądań Serial Session;
- dołączanie stanu sesji i adaptera, których cyklem życia zarządza kod
  wywołujący;
- zwracanie treści tekstowych z zachowaniem sekwencji żądania;
- odczyt niezależnych od transportu metadanych żądania wewnątrz procedury obsługi;
- zwalnianie adaptera przed zniszczeniem routera podczas wycofywania zmian po
  nieudanym uruchomieniu.
