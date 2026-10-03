# Test nakładki CAN-FD na STM32G474

`tests/hardware/stm32_fdcan_canhat` sprawdza natywny backend FDCAN na
NUCLEO-G474RE z nakładką CAN-FD HAT v1.2 od
[Embedded Garage](https://www.youtube.com/@embeddedGarage) (profil płytki
`nucleo-g474re-canhat`). Po resecie stanowisko raz wykonuje wszystkie
sprawdzenia, a potem co dwie sekundy drukuje raport: `JHCANHAT begin` z
podsumowaniem, po jednej linii `JHCANHAT <nazwa> <PASS|FAIL|SKIP> <szczegóły>`
na sprawdzenie i `JHCANHAT end`. Dioda BUSY (PC2) świeci w czasie sprawdzeń;
potem dioda RDY (PC1, wbudowana dioda płytki) miga 1 Hz, gdy nic nie zawiodło,
a 5 Hz w przeciwnym razie.

Bez żadnego okablowania stanowisko sprawdza:

- profil płytki (trzy kanały CAN) i drzewo zegarów: PLL z kwarcu HSE 24 MHz,
  zegar jądra FDCAN 80 MHz z PLL Q, wyłączone rezystory „dead battery" UCPD;
- wewnętrzną i zewnętrzną pętlę zwrotną ramek klasycznych przy 125k, 500k i
  1M oraz ramek CAN FD przy 500k/2M, 500k/4M i 1M/5M na FDCAN1, a także
  500k/2M na FDCAN2 i FDCAN3;
- trzy kanały jednocześnie, z których każdy odbiera tylko własne ramki, oraz
  brak czwartego kanału płytki;
- serię 40 ramek przez kolejkę nadawczą z dokładnymi licznikami dostarczenia
  i przepełnienia oraz wywołania zwrotne odbioru;
- znaczniki czasu nadania co 224 µs przy 500 kbit/s;
- filtry: 28 elementów standardowych i 8 rozszerzonych, decyduje pierwszy
  pasujący filtr, dodawanie i usuwanie filtrów w trakcie ruchu bez
  zatrzymywania kontrolera, odrzucanie ramek zdalnych tylko na zatrzymanym
  kanale;
- samotny węzeł: retransmisja kończy się stanem bus-off, nadawanie
  jednokrotne zawodzi ramka po ramce, ręczne wychodzenie z bus-off czeka na
  `hal_can_recover()` (także po nadaniu blokującym, które zwraca `HAL_EBUS`),
  zatrzymanie kończy ramki z kolejki;
- zmiany trybu: ramki czekające w trybie listen-only kończą się przy
  przełączeniu kanału na klasyczny CAN, zamiast wyjść jako ramki FD, a kanał
  bez FD w trybie odrzuca ramki FD;
- klasyczny slot filtra, który przechodzi na pełną listę rozszerzoną,
  zachowuje stary filtr.

Sprawdzenie `shared_bus` wymaga połączenia CN5, CN6 i CN7 w jedną magistralę
(CAN-H, masa i CAN-L przez wszystkie trzy złącza) i założenia zworek
terminacji dwóch skrajnych kanałów, np. H1 i H3; bez zasilania między CAN-H a
CAN-L powinno być ok. 60 Ω. Każdy kanał wysyła wtedy ramkę klasyczną i ramkę
FD, które pozostałe dwa muszą odebrać. Bez magistrali nikt nie potwierdza
ramek i sprawdzenie zgłasza SKIP. Jeśli magistrala jest spięta, a sprawdzenie
nadal jest pomijane, najpierw sprawdź zasilanie 5 V transceiverów MCP2562FD.

Zbuduj i wgraj przez ST-LINK (OpenOCD):

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/stm32_fdcan_canhat \
  --target stm32g474 --board nucleo-g474re-canhat
vscode/entry/jh-vscode upload \
  --project tests/hardware/stm32_fdcan_canhat \
  --target stm32g474 --board nucleo-g474re-canhat
```

Uruchom weryfikator (przycisk reset powtarza sprawdzenia):

```sh
python3 tests/hardware/stm32_fdcan_canhat/verify_stm32_fdcan_canhat.py \
  --port /dev/serial/by-id/<wirtualny port ST-LINK>
```

ST-LINK może jeszcze trzymać raporty obrazu sprzed wgrania, więc weryfikator
przed przyjęciem raportu czeka pół sekundy ciszy. Drukuje raport jako JSON
i przechodzi, gdy żadne sprawdzenie nie zawiodło; `--require-bus` dodatkowo
traktuje pominięte `shared_bus` jako błąd, do przebiegów ze spiętą
magistralą. Kod wyjścia 0 oznacza sukces, 1 błąd, 2
brak pełnego raportu przed upływem `--timeout` (domyślnie 30 s).
