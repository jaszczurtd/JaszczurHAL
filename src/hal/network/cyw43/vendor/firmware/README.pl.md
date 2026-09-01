Firmware układu WiFi CYW43xx
============================

Ten katalog zawiera bloby z poprawkami firmware, które muszą zostać wgrane do
układu CYW43xx, aby działał prawidłowo.

Firmware WiFi jest uzupełniany do wielokrotności 512 bajtów, po czym dołączany
jest CLM. Tak powstaje jeden scalony plik binarny.

Przykład:

    $ cp 43439A0.bin 43439A0_padded.bin
    $ dd if=/dev/zero of=43439A0_padded.bin bs=1 count=1 seek=$(( ($(stat -c %s 43439A0.bin) / 512) * 512 + 512 - 1))
    $ cat 43439A0_padded.bin 43439A0.clm_blob > 43439A0-7.95.49.00.combined

Plik binarny jest następnie przekształcany w nagłówek, na przykład przez
`xxd -i 43439A0-7.95.49.00.combined`. Makra `CYW43_WIFI_FW_LEN` i
`CYW43_CLM_LEN` określają w bajtach rozmiar oryginalnych plików firmware przed
dopełnieniem.

Firmware Bluetooth dla 43439, stosowanego między innymi w Raspberry Pi Pico W,
jest dostępny jako statyczna tablica w `cyw43_btfw_43439.h` i ma następujący
format:

    1 bajt: liczba znaków w ciągu wersji wraz z terminatorem zerowym
    n bajtów: ciąg wersji zakończony bajtem zerowym
    1 bajt: liczba kolejnych rekordów

    Każdy rekord ma format:
        1 bajt: liczba bajtów danych
        2 bajty: adres
        1 bajt: typ adresu
        n bajtów: dane

Firmware Bluetooth dla 4343A1, stosowanego między innymi w Murata 1DX, jest
dostępny jako statyczna tablica w `cyw43_btfw_4343A1.h`.
