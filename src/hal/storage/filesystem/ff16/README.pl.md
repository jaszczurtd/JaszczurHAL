# Integracja FatFs

Checkout źródeł FatFs R0.16 jest zarządzany w `third_party/FatFs` przez
`scripts/ensure_fatfs.sh` i przypięty do należącego do projektu mirrora
`jaszczurtd/ff16`. Śledzone pliki w tym katalogu zapewniają feature gate
JaszczurHAL, konfigurację projektu i adapter SD przez SPI.

Źródła upstream i licencja pozostają niezmienione w zarządzanym checkoucie.
`third_party/fatfs_version.conf` należy aktualizować wyłącznie przy przyjęciu
sprawdzonego, dokładnego commita.
