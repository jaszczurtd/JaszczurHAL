# Sprzętowy test utraty zasilania podczas zapisu KV na RP

`tests/hardware/rp_kv_power_loss` włącza przeznaczony wyłącznie dla stanowiska
mechanizm fault injection w natywnym providerze flash. Przerywa on wymianę
nieaktywnego banku po unieważnieniu, zapisie treści, jej weryfikacji albo
publikacji. Po każdym przypadku stanowisko ponownie ładuje kopię EEPROM z
fizycznej pamięci flash, tak jak podczas nowego uruchomienia, po czym wspólny
`hal_kv` wybiera bank. Pierwsze trzy przypadki muszą odzyskać poprzednią
wartość, a późny błąd po kompletnej publikacji - nową. Test obejmuje także
odroczony commit dwóch kluczy oraz regresję trybu odczytu z kontrolą nośnika
(ang. read-through). Gdy obraz RAM zawiera niezatwierdzone zmiany, funkcje
odczytujące muszą zwrócić `HAL_EBUSY` i wyzerować wyjściową wartość skalarną
albo długość bloba. Po zatwierdzeniu oraz po ponownym wczytaniu danych
z fizycznej pamięci flash muszą zwrócić nową wartość skalarną i blob.

Stanowisko kasuje i przejmuje całą natywną rezerwację EEPROM/KV. Nie uruchamiaj
go na płytce, której trwałe dane z końca flash muszą zostać zachowane.
Przełącznika fault injection nie wolno używać w firmware aplikacji.

Zbuduj i wgraj przez zwykły workflow:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_kv_power_loss \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_kv_power_loss \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Uruchom weryfikator:

```sh
python3 tests/hardware/rp_kv_power_loss/verify_kv_power_loss.py \
  --port /dev/serial/by-id/<device> --target rp2040
```

Dla Pico 2 użyj `--target rp2350-arm --board pico2` i przekaż ten sam target
weryfikatorowi.
