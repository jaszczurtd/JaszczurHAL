<a id="01---podstawowe-usługi-runtimeu"></a>

# 01 - Podstawowe funkcje systemowe

Przykład pokazuje miganie diodą, diagnostykę urządzenia, timery programowe
oraz regulację PID na symulowanym obiekcie. Nie wymaga dodatkowego sprzętu:
korzysta z diody na płytce i konsoli diagnostycznej.


Wariant `capture` mierzy okresowy sygnał na GPIO0 (PA0 na STM32), korzystając
ze sprzętowych znaczników czasu. Włącza `HAL_ENABLE_PULSE_CAPTURE` i wymaga
zewnętrznego źródła cyfrowego ze wspólną masą. Odbiór danych działa co 1 ms.
