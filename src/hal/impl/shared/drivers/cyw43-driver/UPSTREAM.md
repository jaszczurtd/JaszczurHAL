# cyw43-driver upstream

- Upstream: `https://github.com/georgerobotics/cyw43-driver`
- Commit: `dd7568229f3bf7a37737b9e1ef250c26efe75b23`
- Imported from the pico-sdk submodule bundled with Arduino-Pico 5.4.0.
- Scope: `src/`, the CYW43439 WiFi firmware/NVRAM resources, and both upstream
  license alternatives required by the JaszczurHAL CYW43 host-stack backend.

Upstream C translation units are stored as `*.c.upstream`. This prevents
Arduino library discovery from compiling an unprefixed duplicate implicitly;
standalone target builds must copy/configure an explicit source list and apply
`jh_cyw43_namespace.h` before compilation.

The RP2040 carrier currently links Arduino-Pico's archive built from this exact
driver revision. JaszczurHAL sources include this pinned copy directly so the
portable radio dependency no longer comes from an implicit carrier include
path. A future standalone carrier may compile the vendored sources with the
`jh_*` symbol-prefix map in `jh_cyw43_namespace.h`; this avoids collisions while
Arduino-Pico still owns an unprefixed copy in the final image.
