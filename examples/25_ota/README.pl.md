# 25 - Native RP OTA

Ten przykład włącza ścieżkę native Pico SDK OTA na Pico W i Pico 2 W. Ustaw
dane WiFi i zastąp hasło deweloperskie OTA w `app.c`. Zachowaj tę samą nazwę
hosta, port i hasło w `.vscode/jaszczurhal.project.json`.
Przykład ustala także callback listener hosta na porcie TCP `8266`, dzięki czemu
na hostach filtrujących połączenia przychodzące wystarcza jedna wąska reguła
firewalla. `runmefirst.sh` wykrywa lokalną sieć IPv4 i proponuje trwałe dodanie
tej reguły po pokazaniu jej dokładnego zakresu.

Pełną procedurę projektu, firmware, pierwszego wgrania, VS Code, firewalla,
potwierdzania, rollbacku i odzyskiwania opisuje
[Workflow native RP OTA](../../doc/pl/OTAWorkflow.md).

Aplikacja potwierdza obraz próbny dopiero po uzyskaniu łączności WiFi, po czym
uruchamia uwierzytelnioną usługę OTA. Użyj:

```bash
../../vscode/entry/jh-vscode ota-discover --project "$PWD"
../../vscode/entry/jh-vscode upload-ota --project "$PWD" --interactive
```

W rzeczywistych projektach ustaw `ota.passwordEnv` zamiast zapisywać hasło w
manifeście. Odpowiadające mu hasło urządzenia nadal pozostaje własnością
aplikacji.
