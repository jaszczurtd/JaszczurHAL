# Diagnostyka surowego inquiry HCI Bluetooth Classic

`tests/hardware/bluetooth_classic_hci_trace` rejestruje komendy i zdarzenia HCI
BTstack przed ich interpretacją przez publiczny manager. Ten sam fixture buduje
się dla Pico W RP2040 i Pico 2 W RP2350 ARM. Podaje również liczniki transportu
HCI i zmierzony zegar gSPI CYW43. Adresy Bluetooth są maskowane, treść
nieznanych komend i ciała ACL są ukrywane, a Extended Inquiry Result jest
zachowywany tylko do bajtu RSSI; jego treść EIR jest ukrywana.

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hci_trace \
  --target rp2040 --board picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hci_trace \
  --target rp2350-arm --board pico2w
```

`SCAN` uruchamia jeden dziesięciosekundowy skan, a `SCAN30` trzy kolejne cykle
inquiry. Potem wykonaj `INFO` i `DUMP`. `STOP` sprawdza przerwanie, natomiast
`RESET` usuwa zbuforowane rekordy. Fixture korzysta z prywatnych interfejsów
diagnostycznych i nie należy do publicznego API HAL.

Backend włącza tryb Extended Inquiry Result, więc udany skan pokazuje nazwę
z EIR i RSSI.

Gdy jedna płytka wykrywa urządzenie, a druga nie, porównaj ich ślady. Jeśli
inicjalizacja kontrolera i komenda Inquiry są takie same, a żadna płytka nie
zgłasza utraconych rekordów ani wyczerpania budżetu opróżniania, przyczyna leży
po stronie radiowej (antena, ustawienie płytki, słaba odpowiedź urządzenia),
a nie w parserze wyników, terminie skanu czy transporcie HCI.
