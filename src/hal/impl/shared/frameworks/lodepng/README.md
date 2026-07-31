# LodePNG integration

The upstream LodePNG checkout is managed at `third_party/lodepng` from the
exact commit in `third_party/lodepng_version.conf`. The files in this directory
preserve the JaszczurHAL include path and its default memory-only profile.
The wrapper supplies C linkage for C callers while the managed implementation
is compiled as C++. The RP2350 RISC-V build applies a source-specific optimizer
setting without modifying the managed checkout.

Synchronize or verify the dependency with `scripts/ensure_lodepng.sh`.
