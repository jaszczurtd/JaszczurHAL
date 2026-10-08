# Integracja FatFs

Kopia źródeł FatFs R0.16 jest utrzymywana w `third_party/FatFs` przez
`scripts/ensure_fatfs.sh` na dokładnym commicie z forka `jaszczurtd/ff16`,
który należy do projektu. Pliki przechowywane w tym katalogu zapewniają
warunkowe włączanie funkcji JaszczurHAL, konfigurację projektu i adapter karty
SD korzystający z SPI.

Fork zachowuje źródła i licencję ChaN R0.16 oraz dodaje dwie poprawki:
montowanie odrzuca geometrię FAT, przy której obliczenia rozmiaru się
przepełniają (CVE-2026-6682), a `f_lseek()` wypełnia lukę zerami, gdy rozszerza
plik otwarty do zapisu (CVE-2026-6686). `hal_sd_file_seek()` za koniec takiego
pliku zapisuje więc każdy nowy sektor. `third_party/fatfs_version.conf` należy
aktualizować wyłącznie przy przyjęciu sprawdzonego, dokładnego commitu.
