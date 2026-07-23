# cyw43-driver upstream

- Upstream: `https://github.com/georgerobotics/cyw43-driver`
- Commit: `dd7568229f3bf7a37737b9e1ef250c26efe75b23`
- Imported from the pico-sdk submodule bundled with Arduino-Pico 5.4.0.
- Scope: `src/`, the CYW43439 WiFi firmware/NVRAM resources, and both upstream
  license alternatives required by the JaszczurHAL CYW43 host-stack backend.

Upstream C translation units are stored as `*.c.upstream`. This prevents
recursive source discovery from compiling them without an explicit target
manifest. STM32G474 and RP2040 builds compile the same manifest under the
canonical `cyw43_*` symbols. The RP2040 recipe disables the Arduino-Pico
network archive, network wrappers and automatic Pico W startup, leaving exactly
one JaszczurHAL-owned driver instance in the final image.
