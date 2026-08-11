# TJpgDec integration

The `jaszczurtd/TJpg_Decoder` checkout is managed at
`third_party/TJpg_Decoder` from the exact commit in
`third_party/jpeg_version.conf`.

Only its target-neutral Tiny JPEG Decompressor core is compiled. The tracked
`tjpgd.c` and `tjpgd.h` files gate that core with `HAL_ENABLE_JPEG`; memory input
and RGB565 output adaptation lives in `src/utils/tools.cpp`.

Synchronize or verify the dependency with `scripts/ensure_jpeg.sh`.
