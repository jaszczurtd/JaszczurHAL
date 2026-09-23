# cyw43-driver upstream

- Upstream: `https://github.com/georgerobotics/cyw43-driver`
- Commit: `dd7568229f3bf7a37737b9e1ef250c26efe75b23`
- Imported from the Pico SDK submodule at the pinned upstream revision.
- Scope: `src/`, the CYW43439 WiFi firmware/NVRAM resources, and both upstream
  license alternatives required by the JaszczurHAL CYW43 host-stack backend.

Upstream C translation units are stored as `*.c.upstream`. This prevents
recursive source discovery from compiling them without an explicit target
manifest. STM32G474 and RP2040 builds compile the same manifest under the
standard `cyw43_*` symbols. The RP2040 recipe excludes competing network
archives, wrappers and automatic Pico W startup, leaving exactly one
JaszczurHAL-owned driver instance in the final image.

Bluetooth shared-bus files come from the pinned Pico SDK 2.3.1 at commit
`079c6f39023649b154152db30f1d781e884879bc`; the imported sources are
unchanged since Pico SDK 2.2.0, where they were first taken from. The local
safety delta converts fatal shared-bus assertions and corruption panics into
bounded CYBT errors, validates public buffer/index arguments, and propagates
wake, queue-full and read/write failures to the HCI adapter.
