# 30 - Głośnik Bluetooth

Ten przykład tylko dla RP zamienia Pico W lub Pico 2 W w odbiornik Bluetooth
Classic A2DP o nazwie `JaszczurHAL Speaker`. Odbiera SBC 44,1 lub 48 kHz w
trybie mono, stereo albo joint stereo, miksuje dźwięk do podpisanego PCM mono i
podaje go na taktowane timerem wyjście PWM z DMA. Wariant bazowy zawiera tylko
A2DP, `avrcp` dodaje bezwzględną regulację głośności, a `ble-a2dp` sprawdza
współistnienie BLE oraz Classic/A2DP na wspólnym kontrolerze CYW43. Adapter
wyjścia buforuje wstępnie około 171-186 ms PCM i uzupełnia bufor w kierunku
około 213-232 ms, zależnie od wynegocjowanej częstotliwości próbkowania, aby
pochłaniać jitter źródła i toru radiowego. Przykład rezerwuje 4 KiB stosu dla
aktywnego rdzenia 0, ponieważ pomiar ścieżek SBC i bondingu w pamięci flash
wykazał wyczerpanie bezpiecznego zapasu domyślnego stosu 2 KiB.

Tożsamość inquiry używa Class of Device `0x240414`: klas usług Audio i
Rendering, klasy głównej Audio/Video oraz klasy podrzędnej Loudspeaker. Bit
Rendering jest wymagany do zgodnej z Androidem klasyfikacji A2DP Sink.

## Połączenie

Wyjściem PWM jest **GP6**. Każda próbka PCM jest zamieniana na jeden z 256
poziomów wypełnienia, więc częstotliwość nośna PWM odpowiada wynegocjowanej
częstotliwości próbkowania: 44,1 albo 48 kHz. Nie podłączaj pasywnego głośnika
bezpośrednio do Pico. Minimalny tor sygnałowy wygląda tak:

```text
GP6 ---- 1 kOhm ----+---- wejście wysokoimpedancyjne aktywnego wzmacniacza
                    |
                   10 nF
                    |
GND ----------------+---- masa wzmacniacza
```

Ten filtr dolnoprzepustowy pierwszego rzędu ma częstotliwość graniczną około
15,9 kHz. Gdy ważna jest jakość dźwięku, zastosuj poprawnie zaprojektowany filtr
rekonstrukcyjny drugiego rzędu. Jeśli wejście wzmacniacza nie toleruje
składowej stałej punktu środkowego PWM, dodaj kondensator separujący. Dobierz
zasilanie i moc wzmacniacza do głośnika; pin Pico jest wyłącznie źródłem sygnału
logicznego.

## Budowanie

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

Przy pustym magazynie bondów firmware otwiera jedno 60-sekundowe okno
widoczności i automatycznie zatwierdza żądanie Just Works/PIN wyłącznie podczas
tego okna. Po pierwszej poprawnej ramce SBC menedżer Classic zapisuje wspólny
klucz połączenia z identyfikatorem profilu A2DP. AVRCP nie zapisuje drugiego
klucza. Znany telefon może łączyć się ponownie, gdy urządzenie pozostaje
niewidoczne dla nowych urządzeń.

W Androidzie podczas tego okna otwórz systemowy ekran parowania nowego
urządzenia, wybierz `JaszczurHAL Speaker`, zaakceptuj Just Works i uruchom
odtwarzanie multimediów. Bramka sprzętowa `rp2040:picow` używała źródła Android
oraz filtrowanego i wzmacnianego wyjścia GP6. Bramka runtime
`rp2350-arm:pico2w` używała źródła BlueZ; produkt na tej płytce nadal musi
sprawdzić wybrane fizyczne wyjście audio. Inne źródła i tory wyjściowe wymagają
własnego testu end-to-end.

Dostępne komendy szeregowe to `INFO`, `PAIR`, `RESET` i `WATCHDOG`. `PAIR`
otwiera kolejne ograniczone czasowo okno. `RESET` usuwa trwały bond i
pozostawia parowanie zamknięte do jawnej komendy `PAIR` albo restartu z pustym
magazynem. Użyj `PAIR` po usunięciu głośnika w telefonie: ograniczone okno
wymiany pozostaje otwarte mimo zachowania starego bondu w Pico i zamyka się po
pierwszej poprawnej ramce SBC z nowego połączenia. `WATCHDOG` celowo przestaje
obsługiwać czterosekundowy watchdog, co pozwala sprawdzić reconnect po
rzeczywistym resecie watchdoga; kolejny boot raportuje zapamiętaną przyczynę
resetu. `INFO` podaje format strumienia,
utracone pakiety, odrzucone/uszkodzone ramki, high-water marks ograniczonych
kolejek i pul BTstack, użycie stosu, korekcję zegara, użycie i underruny DMA,
straty adaptera oraz timing CPU dla kontekstu `poll`. Diagnostyka nigdy nie
wypisuje adresu Bluetooth, link key ani treści audio.

W teście sprzętowym sprawdź parowanie, start dźwięku, pauzę/wznowienie/stop,
regulację głośności na obrazie `avrcp`, co najmniej 30 minut odtwarzania,
ponowne łączenie po restarcie urządzenia i telefonu, zimny start bez zadziałania
watchdoga oraz usunięcie bondu komendą `RESET`. Użyj `WATCHDOG` w osobnej
próbie rzeczywistego resetu watchdogiem i ponownego połączenia.

Moduł klasy XY-BT-Mini nie może być źródłem testowym: on również jest
odbiornikiem A2DP. Użyj telefonu, komputera albo dedykowanego nadajnika A2DP.
