# Sprzętowa bramka JH BLE Stream v1

Pełne wymagania, procedurę, kryteria akceptacji i zapisane wyniki zawiera
[główny opis stanowisk sprzętowych](../../../doc/api/pl/03_build_tests.md#bramka-sprzętowa-jh-ble-stream-v1).

## Podstawowy test routera poleceń BLE

Warianty `commands` z `examples/26_ble_stream` sprawdzają osobny adapter
`hal_ble_commands`, podczas gdy bazowy firmware używany w tym teście nadal
sprawdza bezpośrednio dane Stream. System Linux z BlueZ działa w roli Central, a każda
płytka pozostaje urządzeniem Peripheral.

Zbuduj i wgraj obrazy bare metal oraz FreeRTOS na dwie płytki Pico W. Gdy obie są
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

Odczytaj adres BLE każdej płytki z logu USB CDC i uruchom krótki program
weryfikujący dla obu jawnie podanych adresów:

```bash
python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address XX:XX:XX:XX:XX:XX \
  --target rp2040 --board picow --runtime baremetal

python3 tests/hardware/bluetooth_stream/verify_commands.py \
  --address YY:YY:YY:YY:YY:YY \
  --target rp2040 --board picow --runtime freertos
```

Zaliczenie wymaga dokładnego złożenia binarnego echo o rozmiarze 500 bajtów w
obu kierunkach, wartości `BLE_STREAM` wskazującej źródło, wszystkich czterech
flag bezpieczeństwa, niezerowych identyfikatorów drugiej strony i sesji,
`HAL_EPERM` dla trasy ograniczonej do źródła, `HAL_ENOENT` dla nieznanej trasy,
zdarzenia wysłanego przez Peripheral, wymiany żądania i odpowiedzi oraz nowej uwierzytelnionej
sesji po ponownym połączeniu. Podczas pełnej weryfikacji zestawu z dwiema
płytkami zamień obrazy między fizycznymi płytkami i powtórz test.
