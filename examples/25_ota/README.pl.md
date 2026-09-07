<a id="25---natywne-ota-dla-rp"></a>

# 25 - Aktualizacja oprogramowania przez WiFi na płytkach RP

Przykład umożliwia aktualizację OTA na Pico W i Pico 2 W, korzystając z obsługi
OTA opartej na Pico SDK. W `app.c` ustaw dane sieci WiFi i zastąp przykładowe
hasło OTA. Nazwa hosta, port i hasło muszą odpowiadać ustawieniom
w `.vscode/jaszczurhal.project.json`.

Komputer nasłuchuje połączeń zwrotnych na porcie TCP `8266`. Jeżeli zapora
blokuje połączenia przychodzące, potrzebna jest reguła dopuszczająca ten ruch.
`runmefirst.sh` wykrywa lokalną sieć IPv4, pokazuje zakres reguły i proponuje
jej trwałe dodanie.

Przygotowanie projektu, pierwsze wgranie, pracę w VS Code, ustawienia zapory,
potwierdzanie aktualizacji, powrót do poprzedniej wersji i odzyskiwanie przez
BOOTSEL opisuje [Aktualizacja OTA na płytkach RP](../../doc/pl/OTAWorkflow.md).

Aplikacja potwierdza poprawne uruchomienie nowego obrazu dopiero po
połączeniu z WiFi. Następnie uruchamia usługę OTA wymagającą uwierzytelnienia.
Z katalogu tego przykładu uruchom:

```bash
../../vscode/entry/jh-vscode ota-discover --project "$PWD"
../../vscode/entry/jh-vscode upload-ota --project "$PWD" --interactive
```

W docelowym projekcie użyj `ota.passwordEnv`, aby narzędzie na komputerze
odczytywało hasło ze zmiennej środowiskowej zamiast z pliku konfiguracji.
Odpowiadające mu hasło na urządzeniu nadal trzeba ustawić w aplikacji.
