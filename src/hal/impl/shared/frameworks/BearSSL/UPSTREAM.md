# BearSSL source pin

This directory contains the BearSSL core used by the portable JaszczurHAL TLS
provider.

- Upstream fork: `https://github.com/earlephilhower/bearssl-esp8266.git`
- Commit: `aca13833b6f9ddffaea2041a01facc76829dc03b`
- Commit date: 2024-09-23
- Imported from Arduino-Pico 5.4.0, commit
  `0c7d111e9686082f5794462e020c0b5d62c39820`
- License: MIT; see `LICENSE.txt`
- `LICENSE.txt` SHA-256:
  `771bf18c8633ea69ec9b07d2e604c4b99b7bef41b0f5ce6385d24df4448f61ca`
- Normalized upstream `inc/` + `src/` content SHA-256:
  `479235694ffa393b05a95472602811ffc92884f94a52020c578ab7344c4cdbd7`

Files imported from upstream `src/` carry the suffix `.upstream`. This keeps
Arduino's recursive library builder from compiling a second BearSSL beside the
platform archive and producing duplicate `br_*` symbols. CMake consumers use
`cmake/jh_bearssl.cmake` to copy these files to their original `.c` names in
the build tree and compile the pinned source. Arduino-Pico uses its platform
archive, which is built from the exact commit pinned above.

The public JaszczurHAL API never exposes the upstream `br_*` C namespace. No
Arduino `BearSSL::` wrapper, `WiFiClientSecure`, `ClientContext`, or
`StackThunk` source is included.
