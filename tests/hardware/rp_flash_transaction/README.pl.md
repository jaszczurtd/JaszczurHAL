# Sprzętowy test transakcji flash na RP

`tests/hardware/rp_flash_transaction` weryfikuje natywnego koordynatora
flash RP na fizycznym Pico lub Pico 2. Wykonuje operacje rezydujące w RAM z
obu rdzeni, sprawdza, że zajęty kanał DMA RAM->RAM jest tolerowany, a kanał
czytający okno XIP odrzucany, odrzuca callbacki XIP, sprawdza obsługę wejścia
rekurencyjnego, mutuje jeden sektor flash oraz weryfikuje czyszczenie i
odzyskiwanie po zatrzymaniu operacji między kasowaniem a programowaniem.

Druga komenda uruchamia sondę obciążenia: pierścień DMA i skan ADC na
ADC0-ADC2 (GPIO 26-28) pracują, host wpisuje śmieci do portu CDC, a szesnaście
publikowanych zapisów `hal_kv` idzie naprzemiennie z obu rdzeni. Słowa
strażnicze za buforem skanu i licznik bloków dowodzą, że pierścień ani nie
staje, ani nie wychodzi poza bufor, gdy rdzeń trzyma przerwania wyłączone na
czas transakcji flash. Linia statusu podaje powód resetu i zapamiętany rekord
faultu, więc awaria z poprzedniego przebiegu jest nazwana przy kolejnym
zapytaniu.

Sonda zawłaszcza rezerwację EEPROM/KV na końcu flash płytki i sektor tuż pod
nią. Nie uruchamiaj jej na firmware, które przechowuje tam niepowiązane dane.
LED płytki przełącza się przy każdym zapisie KV.

Zbuduj i wgraj wariant bare-metal zgodnie ze zwykłym procesem:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_flash_transaction \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Dla wariantu FreeRTOS SMP dodaj poniższy tymczasowy wpis cache do manifestu
i uruchom te same komendy kompilacji/wgrywania:

```json
"JH_EXTRA_DEFINES": "HAL_ENABLE_FREERTOS=1"
```

Usuń wpis cache przed ponowną kompilacją wariantu bare-metal.

Uruchom weryfikator:

```sh
python3 tests/hardware/rp_flash_transaction/verify_flash_transaction.py \
  --port /dev/serial/by-id/<device>
```
