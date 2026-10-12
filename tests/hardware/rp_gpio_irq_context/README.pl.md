# Sprzętowy test kontekstu przerwań GPIO na RP

`tests/hardware/rp_gpio_irq_context` sprawdza przerwania GPIO z kontekstem na
fizycznym Pico lub Pico 2. Jeden handler obsługuje kilka pinów i musi rozróżnić
instancje po kontekście, z którym został zarejestrowany - czyli dokładnie to, co
musi zadziałać we wspólnym dispatchu `IO_IRQ_BANK0`.

Test nie wymaga żadnego zworkowania i nie steruje pinem: zbocza powstają z
przełączania wewnętrznego pull-a między dołem a górą, więc jest bezpieczny na
płytce o nieznanym otoczeniu. Używa GP2, GP3, GP4 i GP5.

Plik `device_tree.json` opisuje sprzęt każdego montażu: `pico` dla Pico,
`pico2-arm` i `pico2-riscv` dla Pico 2, `picow` dla Pico W oraz `pico2w` dla
Pico 2 W. Piny sondy to jego węzły wejściowe `ctxA` (GP2), `ctxB` (GP3),
`plain` (GP4) i `swap` (GP5), które między impulsami trzyma wewnętrzny
pull-up. Firmware i weryfikator biorą piny z tego pliku.

Co firmware sprawdza na krzemie:

- dwa piny na wspólnym handlerze trafiają we własny kontekst i raportują własny
  numer pinu;
- callback bez kontekstu na trzecim pinie nadal działa obok nich i nie rusza ich
  liczników;
- oba rodzaje handlerów podmieniają się na jednym pinie, w obie strony;
- odłączenie zatrzymuje każdy handler i czyści zapisanego właściciela rdzenia.

Skompiluj i wykonaj pierwsze wgranie BOOTSEL:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_gpio_irq_context \
  --assembly pico
vscode/entry/jh-vscode upload-uf2 \
  --project tests/hardware/rp_gpio_irq_context \
  --assembly pico
```

Dla innej płytki podaj w `--assembly` klucz jej montażu, także weryfikatorowi.

Uruchom weryfikator z montażem, dla którego zbudowano firmware; piny zgłoszone
dla `ctxA` i `ctxB` porównuje właśnie z tym montażem:

```sh
python3 -m pip install pyserial
python3 tests/hardware/rp_gpio_irq_context/verify_gpio_irq_context.py \
  --port /dev/serial/by-id/<urządzenie> --assembly pico
```

Firmware odpowiada na pojedynczą komendę `T` jedną linią wyniku. `checks` to
liczba wykonanych asercji, `failed` to maska tych, które nie przeszły, a pary
`hits/pin` dla każdej sondy pozwalają odróżnić błąd dispatchu od pinu, do
którego po prostu nie dotarło żadne zbocze. Zostaw piny GP2..GP5 nieobciążone -
cokolwiek trzyma któryś z nich, przebije wewnętrzny pull i sonda zaraportuje dla
niego zero trafień.
