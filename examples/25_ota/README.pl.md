# 25 - Natywne OTA dla RP

Ten przykład włącza natywną obsługę OTA opartą na Pico SDK na Pico W i Pico 2 W.
Ustaw dane WiFi i zastąp deweloperskie hasło OTA w `app.c`. Zachowaj tę samą
nazwę hosta, port i hasło w `.vscode/jaszczurhal.project.json`.
Przykład ustala także port TCP `8266`, na którym host nasłuchuje wywołań
zwrotnych. Dzięki temu na hostach filtrujących połączenia przychodzące wystarcza
jedna precyzyjna reguła zapory sieciowej. `runmefirst.sh` wykrywa lokalną sieć
IPv4 i po pokazaniu dokładnego zakresu reguły proponuje jej trwałe dodanie.

Pełną procedurę przygotowania projektu i firmware, pierwszego wgrania, obsługi
w VS Code, konfiguracji zapory, potwierdzania obrazu, wycofywania aktualizacji i
odzyskiwania opisuje [Proces natywnej aktualizacji OTA dla RP](../../doc/pl/OTAWorkflow.md).

Aplikacja potwierdza obraz próbny dopiero po uzyskaniu łączności WiFi, po czym
uruchamia uwierzytelnioną usługę OTA. Użyj:

```bash
../../vscode/entry/jh-vscode ota-discover --project "$PWD"
../../vscode/entry/jh-vscode upload-ota --project "$PWD" --interactive
```

W rzeczywistych projektach ustaw `ota.passwordEnv` zamiast zapisywać hasło w
manifeście. Odpowiadające mu hasło po stronie urządzenia nadal definiuje
aplikacja.
