# Integracja FatFs

Kopia źródeł FatFs R0.16 jest utrzymywana w `third_party/FatFs` przez
`scripts/ensure_fatfs.sh` na dokładnym commicie z mirrora `jaszczurtd/ff16`,
który należy do projektu. Pliki przechowywane w tym katalogu zapewniają
warunkowe włączanie funkcji JaszczurHAL, konfigurację projektu i adapter karty
SD korzystający z SPI.

Źródła upstreamowe i licencja pozostają niezmienione w kopii utrzymywanej przez
projekt.
`third_party/fatfs_version.conf` należy aktualizować wyłącznie przy przyjęciu
sprawdzonego, dokładnego commitu.
