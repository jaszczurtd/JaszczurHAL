# Fixture urządzenia Bluetooth Classic HID

Pełna procedura i zapisany wynik znajdują się w
[głównym opisie stanowisk sprzętowych](../../../doc/api/pl/03_build_tests.md#sprzętowy-test-bluetooth-classic-hid-host-innej-klasy).

Ten prywatny fixture dla Pico W ogłasza standardową mysz Bluetooth Classic HID,
akceptuje parowanie Just Works i wysyła naprzemienne surowe raporty myszy. Służy
wyłącznie do weryfikacji publicznego managera Classic i ogólnego HID Host
JaszczurHAL na drugim radiu; nie jest publicznym API urządzenia HID.

Zbuduj go dla `rp2040:picow`, wgraj wyłącznie do wskazanej płytki peryferyjnej,
a na płytce hosta uruchom wariant `hid-host` przykładu 29. `INFO` na fixture
musi zgłosić `controller=1`. Na hoście użyj `SCAN` i zatwierdź żądanie szeregową
komendą `AUTHORIZE`. Kryterium akceptacji wymaga `JHC85-HID-PASS`; następne
`INFO` musi zgłosić deskryptor, input i zapis peera, a `INFO` fixture połączenie
HID oraz niezerowy licznik raportów.

Fixture przechowuje link key wyłącznie w RAM. Restart dowolnej płytki czyści jej
lokalny stan testowy, więc ta procedura nie weryfikuje trwałego bondingu.
