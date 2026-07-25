# RP2040 CYW43 PIO transport provenance

`rp2040_cyw43_pio_program.h` and the transfer sequence in
`rp2040_cyw43_gspi.cpp` are derived from:

- upstream: `https://github.com/raspberrypi/pico-sdk`
- commit: `8fcd44a1718337861214ba5499a8faceea2bfa1d`
- paths:
  `src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.pio` and
  `src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c`
- license: BSD-3-Clause

The generated instruction words for upstream's `spi_gap01_sample0` (high-speed)
and `spi_gap0_sample1` (low-speed) programs are kept in JaszczurHAL source so
the firmware build does not require `pioasm` or prebuilt CYW43 transport
objects. JaszczurHAL derives the 16.8 PIO divider from the live
`clk_sys`, selects the timing program, and supplies its own transport
lifecycle, timeout handling and generic gSPI bridge.
