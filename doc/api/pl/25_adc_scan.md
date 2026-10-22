# Sprzętowo taktowany skan ADC

Włącz `HAL_ENABLE_ADC_SCAN` i dołącz `<hal/analog/hal_adc_scan.h>`, aby
próbkować stały zestaw pinów w sposób ciągły, ze sprzętowo wyznaczanym okresem.
Przetwornik obsługuje piny jeden po drugim, DMA zapisuje wyniki do dwóch połówek
bufora, który należy do aplikacji, a aplikacja czyta tylko gotowe bloki. Każda
próbka ma znane położenie w czasie, a CPU nigdy nie czeka na konwersję.

Poniższy przykład używa GPIO26, GPIO28 i wewnętrznego czujnika temperatury
na RP. Numery pinów i komentarze dotyczące czasu odnoszą się do tej platformy.

```c
static uint16_t blocks[2u * 400u * 3u] __attribute__((aligned(4)));

hal_adc_scan_config_t config = {0};
config.pins[0] = 26u;
config.pins[1] = 28u;
config.pins[2] = HAL_ADC_SCAN_PIN_TEMPERATURE;
config.pin_count = 3u;
config.conversion_period_ns = 4000u; /* każdy pin co 12 us */
config.buffer = blocks;
config.block_frames = 400u;          /* jeden blok co 4,8 ms */
hal_status_t status = hal_adc_scan_start(&config);

/* W pętli aplikacji. */
hal_adc_scan_block_t block;
if (status == HAL_OK && hal_adc_scan_take(&block) == HAL_OK) {
    const uint8_t shunt = hal_adc_scan_pin_position(26u);
    for (uint32_t k = 0u; k < block.frames; ++k) {
        const uint16_t raw = block.samples[(k * block.pin_count) + shunt];
        /* k * hal_adc_scan_frame_period_ns() to położenie próbki w czasie. */
        (void)raw;
    }
}
```

Na STM32G474 wybierz piny ADC1 w kodowaniu HAL, np. PA0 jako `0u` i PA1 jako
`1u`, i zmień też pin przekazywany do `hal_adc_scan_pin_position()`.
Dołączenie temperatury może wydłużyć rzeczywisty okres ramki. Na ESP32-S3
wybierz dostępne GPIO ADC1, pomiń `HAL_ADC_SCAN_PIN_TEMPERATURE`, dostosuj
`pin_count` i bufor oraz ustaw co najmniej 12000 ns na konwersję.
Rzeczywisty okres na każdej platformie odczytasz przez
`hal_adc_scan_frame_period_ns()`.

Wariant `SCAN` przykładu [13_adc](../../../examples/13_adc/) wypisuje
statystyki bloków na targetach RP i STM32G474.

## Bloki, pozycje i znacznik

Ramka zawiera po jednej próbce każdego pinu. Ramki leżą w bloku jedna za drugą,
a pozycję pinu w ramce podaje `hal_adc_scan_pin_position()`: przetwornik RP
oddaje wejścia w kolejności rosnącej niezależnie od kolejności w konfiguracji,
STM32G474 i ESP32-S3 zachowują kolejność z konfiguracji.
`hal_adc_scan_frame_period_ns()` to czas między dwiema próbkami jednego pinu,
tak jak przetwornik naprawdę pracuje; backendy zaokrąglają żądany okres
konwersji w górę do tego, co potrafią.

`hal_adc_scan_take()` wydaje najnowszy ukończony blok raz i zwraca `HAL_EAGAIN`
do chwili ukończenia następnego. Blok pozostaje ważny do ukończenia kolejnego,
więc przetwórz go albo skopiuj w ciągu jednego okresu bloku; blok, o który
wolny konsument nie zapytał, nie jest kolejkowany - widać go tylko jako lukę
w `sequence`. `hal_adc_scan_latest()` zwraca najnowszą próbkę pinu, z bloku
w trakcie zapisu, jeśli ma już pełną ramkę.

Opcjonalny hook `marker` uruchamia się przy ukończeniu bloku, a jego wynik
wędruje z blokiem. Na RP i STM32G474 dzieje się to w przerwaniu DMA na rdzeniu,
który uruchomił skan: co najwyżej kilka odczytów, bez wywołań HAL, bez blokad.
Służy do sparowania bloku na przykład z licznikiem PWM. Na ESP32-S3 hook
i `completed_us` pobiera zadanie, które odbiera blok: zarówno `take()`, jak i `latest()` zbierają
strumień sterownika przed odpowiedzią, więc najnowsza próbka jest świeża
niezależnie od tego, które z nich aplikacja wywołuje. Pierścień sterownika
mieści dwa bloki rekordów, czyli tyle, na ile aplikacja może odwrócić uwagę
między dwoma wywołaniami; build ze skanem robi przerwanie sterownika
IRAM-safe, więc zapis do flash (pamięć podręczna wyłączona na dziesiątki
milisekund) gubi ramki z pierścienia, ale nigdy nie zatrzymuje strumienia.

Na RP pierścień pracuje wyłącznie na DMA: każdy kanał danych po bloku uruchamia
kanał sterujący, który cofa wskaźnik zapisu drugiego kanału na początek jego
połowy, więc CPU tylko publikuje gotowe bloki. Rdzeń, który trzyma przerwania
wyłączone dłużej niż jeden blok, na przykład w czasie transakcji flash, gubi
bloki: przerwanie dolicza wtedy do `sequence` najwyżej dwa z nich, a przerwę
widać w `completed_us`, ale pierścień nigdy nie wychodzi poza bufor. Skan
zajmuje cztery kanały DMA. Na STM32G474 kołowy kanał DMA prowadzi pierścień
tak samo; spóźnione przerwanie z obiema flagami połówek publikuje połowę,
której kanał akurat nie wypełnia.

## Własność

Start i stop należą do jednego rdzenia właściciela; take i latest są
serializowane i nie wolno ich wołać z ISR. Podczas skanu przetwornik należy do
skanu. Na RP i STM32G474 tor audio DACless i skan wykluczają się nawzajem przez
`HAL_EBUSY`, a `hal_adc_read()` skanowanego pinu zwraca najnowszą próbkę skanu
zamiast konwertować; pin spoza skanu czyta w tym czasie 0, tak jak pod DACless.
Na ESP32-S3 skan trzyma ADC1 w trybie ciągłym, a `hal_adc_read()` skanowanego
pinu jest obsługiwane tak samo.

## Ograniczenia backendów

| Target | Okres | Piny | Uwagi |
| --- | --- | --- | --- |
| RP2040 / RP2350 | od 2 µs, w pełnych cyklach 48 MHz | do 5 (GPIO26..29 i temperatura) | cztery kanały DMA, `DMA_IRQ_0` na wyłączność |
| STM32G474 | żądany okres od 1 µs; zaokrąglany w górę do sprzętowego kroku | do 8 | sekwencja regularna ADC1, DMA1 kanał 3 kołowo |
| ESP32-S3 | od 12 µs (83,3 kS/s) | tylko pady ADC1, bez temperatury | sterownik ciągły ESP-IDF |

Na STM32G474 obecność czujnika temperatury wydłuża czas próbkowania całej
sekwencji do co najmniej 247,5 cyklu ADC. Razem z 12,5 cyklu konwersji daje
to około 6,12 µs na pin przy zegarze ADC 42,5 MHz albo 6,5 µs przy 40 MHz.
Wynikowy okres ramki odczytasz przez `hal_adc_scan_frame_period_ns()`.

`hal_adc_scan_start()` odrzuca okres poniżej
`HAL_ADC_SCAN_MIN_CONVERSION_NS` (1000 ns) przez `HAL_EINVAL`, zanim wywoła
backend. Zwraca `HAL_EUNSUPPORTED` dla okresu lub pinu, którego
backend nie obsłuży, `HAL_EBUSY`, gdy przetwornik albo przerwanie są już zajęte,
i `HAL_ENOMEM`, gdy nie ma wolnego kanału DMA. Stop jest idempotentny i zwalnia
wszystko, także po nieudanym starcie.

Backendy RP2040, STM32G474 i ESP32-S3 są sprawdzone na sprzęcie
(`tests/hardware/rp_flash_transaction`, `stm32_storage_scan` i
`esp32s3_adc_scan`); test sprzętowy RP2350 oczekuje na wykonanie.
