# FatFs integration

The FatFs R0.16 source checkout is managed in `third_party/FatFs` by
`scripts/ensure_fatfs.sh`. It is pinned to the project-owned
`jaszczurtd/ff16` mirror. The tracked files in this directory provide the
JaszczurHAL feature gate, project configuration, and SD-over-SPI adapter.

The upstream sources and license remain unchanged in the managed checkout.
Update `third_party/fatfs_version.conf` only when adopting a reviewed exact
commit.
