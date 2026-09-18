# Sprzętowy test transakcji flash na RP

`tests/hardware/rp_flash_transaction` weryfikuje natywnego koordynatora
flash RP na fizycznym Pico lub Pico 2. Wykonuje operacje rezydujące w RAM z
obu rdzeni, weryfikuje odrzucanie aktywnego DMA i callbacków XIP, sprawdza
obsługę wejścia rekurencyjnego, mutuje ostatni sektor flash oraz weryfikuje
czyszczenie i odzyskiwanie po zatrzymaniu operacji między kasowaniem a
programowaniem.

Sonda celowo zawłaszcza ostatni sektor flash płytki. Nie uruchamiaj jej na
firmware, które przechowuje tam niepowiązane dane.

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
