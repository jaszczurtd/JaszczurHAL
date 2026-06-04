#pragma once
/*
 * Minimal SPI.h stub for Arduino platform - host/test builds only.
 * SPISettings/SPI are referenced by HAL_ENABLE_SDLOGGER builds and by selected
 * Arduino-compatible sources during host diagnostics.
 */
#include <stdint.h>

#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif
#ifndef SPI_MODE1
#define SPI_MODE1 1
#endif
#ifndef SPI_MODE2
#define SPI_MODE2 2
#endif
#ifndef SPI_MODE3
#define SPI_MODE3 3
#endif

class SPISettings {
public:
    SPISettings() = default;
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass {
public:
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
};

inline SPIClass SPI;
