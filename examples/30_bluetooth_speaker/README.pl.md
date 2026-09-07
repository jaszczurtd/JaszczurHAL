<a id="30---głośnik-bluetooth"></a>

# 30 - Odtwarzanie dźwięku przez Bluetooth

Przykład zamienia Pico W lub Pico 2 W w odbiornik Bluetooth Classic A2DP
widoczny jako `JaszczurHAL Speaker`. Odbiera dźwięk SBC z częstotliwością
44,1 lub 48 kHz, w trybie mono, stereo albo joint stereo. Po dekodowaniu
miksuje go do próbek PCM mono ze znakiem i odtwarza przez wyjście PWM na
GP6. Przesyłaniem próbek sterują timer i DMA.

Wersja podstawowa obsługuje A2DP. Wariant `avrcp` dodaje bezwzględną
regulację głośności, a `ble-a2dp` kompiluje obsługę BLE i Classic/A2DP
wspólnie dla kontrolera CYW43. Przykład jest dostępny tylko dla rodziny RP.

## Połączenie

**Nie podłączaj pasywnego głośnika bezpośrednio do Pico.** Wyjście GP6
przekazuje sygnał do wzmacniacza, a nie zasila głośnik. Każda próbka PCM
jest zamieniana na jeden z 256 poziomów wypełnienia PWM. Częstotliwość
nośna wynosi 44,1 lub 48 kHz, zgodnie z częstotliwością próbkowania.

Minimalne połączenie z aktywnym wzmacniaczem:

```text
GP6 ---- 1 kOhm ----+---- wejście wysokoimpedancyjne aktywnego wzmacniacza
                    |
                   10 nF
                    |
GND ----------------+---- masa wzmacniacza
```

Filtr dolnoprzepustowy 1 kΩ/10 nF ma częstotliwość graniczną około 15,9 kHz.
Dla lepszej jakości dźwięku zastosuj poprawnie zaprojektowany filtr
rekonstrukcyjny drugiego rzędu. Jeśli wejście wzmacniacza nie toleruje
składowej stałej wynikającej ze środkowego poziomu PWM, dodaj kondensator
separujący. Połącz masy i dobierz zasilanie oraz moc wzmacniacza do głośnika.

<a id="budowanie"></a>

## Kompilacja

Uruchom z głównego katalogu repozytorium:

```bash
./scripts/examples_dispatcher.py build --target rp2040 \
  --example 30_bluetooth_speaker
./scripts/examples_dispatcher.py build --target rp2350-arm \
  --example 30_bluetooth_speaker

vscode/entry/jh-vscode build --project examples/30_bluetooth_speaker \
  --target rp2040 --board picow --variant avrcp
vscode/entry/jh-vscode build --project examples/30_bluetooth_speaker \
  --target rp2350-arm --board pico2w --variant ble-a2dp
```

## Parowanie i odtwarzanie

Gdy nie ma zapisanego urządzenia, program otwiera jedno 60-sekundowe okno
parowania. Tylko w tym czasie jest wykrywalny i automatycznie zatwierdza
oczekujące żądania Just Works/PIN. Po odebraniu pierwszej poprawnej ramki
SBC zapisuje wspólny klucz połączenia z identyfikatorem profilu A2DP.
AVRCP korzysta z tego samego klucza, zamiast zapisywać drugi. Znany telefon
może później połączyć się ponownie, mimo że odbiornik nie jest wykrywalny
dla nowych urządzeń.

Na telefonie z Androidem otwórz ekran parowania nowego urządzenia w ciągu
tego okna. Wybierz `JaszczurHAL Speaker`, zaakceptuj żądanie Just Works
i rozpocznij odtwarzanie. Automatyczna zgoda na parowanie służy temu
przykładowi; w produkcie dobierz sposób zatwierdzania do wymagań dostępu.

Urządzenie przedstawia się klasą Class of Device `0x240414`: usługami Audio
i Rendering, klasą główną Audio/Video oraz klasą szczegółową Loudspeaker.
Bit Rendering zapewnia klasyfikację odbiornika A2DP zgodną z Androidem.

## Polecenia konsoli

| Polecenie | Działanie |
|---|---|
| `INFO` | Wyświetla stan strumienia oraz liczniki diagnostyczne. |
| `PAIR` | Otwiera kolejne ograniczone czasowo okno parowania. |
| `RESET` | Usuwa zapisane dane parowania. Nie otwiera od razu kolejnego okna. |
| `WATCHDOG` | Celowo przestaje obsługiwać watchdog, aby wymusić reset po czterech sekundach. |

Użyj `PAIR`, gdy usuniesz głośnik z listy urządzeń telefonu. Okno pozwala
zastąpić poprzednie dane parowania, nawet jeśli Pico nadal je pamięta.
Zamyka się po pierwszej poprawnej ramce SBC z nowego połączenia.
Po `RESET` parowanie pozostaje zamknięte do polecenia `PAIR` albo restartu
z pustą pamięcią urządzeń. `WATCHDOG` służy do osobnego testu ponownego
łączenia po rzeczywistym resecie; przy następnym uruchomieniu program
wypisuje zapamiętaną przyczynę resetu.

## Buforowanie i diagnostyka

Przed rozpoczęciem odtwarzania program zbiera około 171-186 ms dźwięku.
Następnie uzupełnia bufor do około 213-232 ms. Wartości zależą od
częstotliwości próbkowania; bufor ogranicza wpływ nierównego tempa dostaw
pakietów ze źródła i przez radio. Konfiguracja rezerwuje 4 KiB stosu
rdzenia 0. Wcześniejsze pomiary dekodowania SBC i zapisu danych parowania
do flash wykazały zbyt mały zapas przy domyślnych 2 KiB.

`INFO` podaje format strumienia, straty pakietów, odrzucone lub uszkodzone
ramki, maksymalne wykorzystanie kolejek i pul BTstack oraz użycie stosu.
Obejmuje też korekcję zegara, wykorzystanie DMA, przypadki braku próbek
do odtworzenia, straty w obsłudze wyjścia oraz czas pracy CPU w `poll`.
Diagnostyka nie wypisuje adresów Bluetooth, kluczy połączenia ani treści
przesyłanego dźwięku.

## Zakres testów sprzętowych

Test `rp2040:picow` obejmował telefon POCO M8 z Androidem oraz pin GP6 podłączony
przez filtr do wzmacniacza. Dla `rp2350-arm:pico2w` sprawdzono działanie
programu ze źródłem BlueZ; wybrane fizyczne wyjście audio wymaga nadal
osobnej weryfikacji. Inne źródła i układy wyjściowe również trzeba
sprawdzić jako kompletne połączenie.

