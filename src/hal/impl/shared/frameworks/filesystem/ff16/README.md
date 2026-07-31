# FatFs integration

The FatFs R0.16 source archive is managed in `third_party/FatFs` by
`scripts/ensure_fatfs.sh`. The tracked files in this directory provide the
JaszczurHAL feature gate, project configuration, and SD-over-SPI adapter.

The upstream sources and license remain unchanged in the generated checkout.
Update `third_party/fatfs_version.conf` when adopting a different authenticated
archive.
