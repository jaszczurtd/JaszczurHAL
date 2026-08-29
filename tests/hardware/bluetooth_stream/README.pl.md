# Sprzętowy gate JH BLE Stream v1

Pełne wymagania, procedurę, kryteria akceptacji i zapisane wyniki zawiera
[główny opis fixture sprzętowych](../../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1).

## Smoke test routera poleceń BLE

Warianty `commands` z `examples/26_ble_stream` sprawdzają osobny adapter
`hal_ble_commands`, podczas gdy bazowy firmware tego fixture nadal sprawdza
surowe payloady Stream. Linux/BlueZ pełni rolę Centrala, a każdy board pozostaje
Peripheralem.

Zbuduj i wgraj obrazy bare-metal oraz FreeRTOS na dwa boardy Pico W. Gdy oba są
już w BOOTSEL, wybierz jawnie każdy wolumin:

```bash
vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands \
  --bootsel-volume /dev/<baremetal-partition>

vscode/entry/jh-vscode upload \
  --project examples/26_ble_stream \
  --target rp2040 --board picow --variant commands-freertos \
  --bootsel-volume /dev/<freertos-partition>
```

Odczytaj adres BLE każdego boardu z logu USB CDC i uruchom krótki weryfikator
dla obu jawnych adresów:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 --board picow --runtime baremetal

python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address YY:YY:YY:YY:YY:YY \
  --target rp2040 --board picow --runtime freertos
```

Zaliczenie wymaga dokładnego złożenia binarnego echo 500 bajtów w obu
kierunkach, provenance `BLE_STREAM`, wszystkich czterech flag bezpieczeństwa,
niezerowego peer i identyfikatora sesji, `HAL_EPERM` dla trasy ograniczonej do
źródła, `HAL_ENOENT` dla nieznanej trasy, zdarzenia Peripheral, wymiany
żądanie/odpowiedź oraz świeżej uwierzytelnionej sesji po jednym ponownym
połączeniu. Podczas pełnej weryfikacji fixture dwóch boardów zamień obrazy
między fizycznymi boardami i powtórz test.
