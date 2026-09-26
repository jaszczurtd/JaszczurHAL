# Test zapisu danych i skanu ADC na STM32G474

`tests/hardware/stm32_storage_scan` sprawdza magazyn klucz-wartość nad
rezerwacją EEPROM we flash oraz pierścień skanu ADC na NUCLEO-G474RE.
Nieparzysty rozruch wykonuje sprawdzenia: inicjalizację KV, licznik rozruchów i
blob z nazwą, pięć bloków skanu, dziesięć przebiegów z przerwaniami wyłączonymi
dłużej niż jeden blok (spóźnione przerwanie ma opublikować połowę, której DMA
akurat nie wypełnia), cztery publikacje KV do flash w czasie skanu (pierścień
ma pracować dalej), a potem zapisuje werdykt w KV i resetuje układ przez
watchdog. Parzysty rozruch potwierdza, że licznik, nazwa i werdykt przetrwały,
skanuje dalej i co dwie sekundy drukuje `JHSTM32REPORT`. Dioda (PA5) przełącza
się co blok skanu w czasie sprawdzeń, a w czasie raportowania miga 1 Hz. PA0 i
PA1 mogą pozostać niepodłączone; oceniana jest geometria pierścienia, nie
napięcia.

Stanowisko jest właścicielem rezerwacji EEPROM/KV na końcu flash. Licznik
rozruchów rośnie także między kolejnymi wgraniami, co jest dodatkowym dowodem
trwałości.

Zbuduj i wgraj przez ST-LINK (OpenOCD):

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/stm32_storage_scan \
  --target stm32g474 --board nucleo-g474re
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_storage_scan \
  --target stm32g474 --board nucleo-g474re
```

Uruchom weryfikator zaraz po wgraniu (płytka startuje i sama przechodzi obie
fazy; przycisk reset powtarza przebieg):

```sh
python3 tests/hardware/stm32_storage_scan/verify_stm32_storage_scan.py \
  --port /dev/serial/by-id/<wirtualny port ST-LINK>
```

Weryfikator przechodzi, gdy raport parzystego rozruchu ma wszystkie bity
werdyktu, 10/10 przebiegów z maską, 4/4 zapisy KV, `persist=1`, `wdg=1`, co
najmniej cztery klucze, pojemność indeksu co najmniej 32 i pracujący skan.
