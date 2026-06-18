# Example 36: LodePNG

Encodes a tiny 2x2 RGBA image to PNG bytes in memory, decodes it back to RGBA,
converts it to RGB565, then repeats the decode path from a Base64 PNG string.

Enabled module:

- `HAL_ENABLE_PNG_AS_BASE64` (propagates `HAL_ENABLE_CRYPTO` and
  `HAL_ENABLE_PNG`)

JaszczurHAL's default LodePNG profile keeps the memory-based C API and disables
the upstream disk helpers and C++ `std::vector` wrapper. This example does not
require external wiring.
