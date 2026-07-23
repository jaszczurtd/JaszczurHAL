# RP2040 CYW43 PIO transport provenance

`rp2040_cyw43_pio_program.h` and the transfer sequence in
`rp2040_cyw43_gspi.cpp` are derived from:

- upstream: `https://github.com/raspberrypi/pico-sdk`
- commit: `8fcd44a1718337861214ba5499a8faceea2bfa1d`
- paths:
  `src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.pio` and
  `src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c`
- license: BSD-3-Clause

The generated PIO instruction words are kept in JaszczurHAL source so the
firmware build does not require `pioasm` or the Arduino-Pico CYW43 transport
objects. JaszczurHAL supplies its own transport lifecycle, timeout handling and
generic gSPI bridge around the program.
