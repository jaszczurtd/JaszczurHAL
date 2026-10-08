# FatFs integration

The FatFs R0.16 source checkout is managed in `third_party/FatFs` by
`scripts/ensure_fatfs.sh`. It is pinned to the project-owned
`jaszczurtd/ff16` fork. The tracked files in this directory provide the
JaszczurHAL feature gate, project configuration, and SD-over-SPI adapter.

The fork keeps ChaN's R0.16 sources and license, with two fixes: mounting
rejects FAT geometry whose size calculation overflows (CVE-2026-6682), and
`f_lseek()` fills the gap with zeros when it extends a file opened for writing
(CVE-2026-6686). `hal_sd_file_seek()` past the end of such a file therefore
writes every new sector. Update `third_party/fatfs_version.conf` only when
adopting a reviewed exact commit.
