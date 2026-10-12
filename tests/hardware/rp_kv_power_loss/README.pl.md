# Sprzętowy test utraty zasilania podczas zapisu KV na RP

`tests/hardware/rp_kv_power_loss` włącza przeznaczony wyłącznie dla stanowiska
mechanizm fault injection w natywnym providerze flash. Przerywa on dwa rodzaje
zapisu. Commit dziennika, dopisywany do aktywnego banku, zostaje przerwany po
połowie bajtów albo po wszystkich. Kompaktowanie do drugiego banku zostaje
przerwane po unieważnieniu, zapisie treści, jej weryfikacji albo publikacji.
Po każdym przypadku stanowisko ponownie ładuje kopię EEPROM z fizycznej pamięci
flash, tak jak podczas nowego uruchomienia, po czym wspólny `hal_kv` wybiera
bank. Commit zapisany do połowy i pierwsze trzy przypadki kompaktowania muszą
odzyskać poprzednią wartość, a późny błąd po kompletnym commicie albo
publikacji - nową. Test obejmuje także odroczony commit dwóch kluczy oraz
regresję trybu odczytu z kontrolą nośnika (ang. read-through). Gdy obraz RAM
zawiera niezatwierdzone zmiany, funkcje odczytujące muszą zwrócić `HAL_EBUSY`
i wyzerować wyjściową wartość skalarną albo długość bloba. Po zatwierdzeniu
oraz po ponownym wczytaniu danych z fizycznej pamięci flash muszą zwrócić nową
wartość skalarną i blob.

Na końcu stanowisko mierzy czas zapisów: najdłuższy z dziesięciu commitów
dziennika, kasowanie zapasowego banku przez `hal_kv_prepare_ex()` oraz
kompaktowanie, które ten bank przyjmuje już bez kasowania. Weryfikator wymaga
commitu dziennika poniżej 10 ms i kompaktowania poniżej 20 ms; czas kasowania
tylko raportuje.

Wbudowana dioda miga z częstotliwością 1 Hz, gdy stanowisko czeka, i świeci
w trakcie testu.

Stanowisko kasuje i przejmuje całą natywną rezerwację EEPROM/KV. Nie uruchamiaj
go na płytce, której trwałe dane z końca flash muszą zostać zachowane.
Przełącznika fault injection nie wolno używać w firmware aplikacji.

Zbuduj i wgraj przez zwykły workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_kv_power_loss \
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_kv_power_loss \
  --assembly pico \
  --port /dev/serial/by-id/<device>
```

Uruchom weryfikator:

```sh
python3 tests/hardware/rp_kv_power_loss/verify_kv_power_loss.py \
  --port /dev/serial/by-id/<device> --target rp2040
```

Plik `device_tree.json` opisuje sprzęt każdego montażu: `pico` dla Pico
i `pico2-arm` dla Pico 2. Test potrzebuje tylko samej płytki. Dla Pico 2 zbuduj
i wgraj firmware z `--assembly pico2-arm`, a weryfikatorowi przekaż
`--target rp2350-arm`.
