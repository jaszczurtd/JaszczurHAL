#pragma once
/*
 * Arduino-compatible SPI.h for non-Arduino targets (host tests and STM32).
 * The class surface intentionally follows the common Arduino SPI API while
 * delegating all work to hal_spi, so ported Arduino drivers do not bypass the
 * target backend.
 */
#include "hal/hal_spi.h"
#include <stddef.h>
#include <stdint.h>

#ifndef SPI_INTERFACES_COUNT
#define SPI_INTERFACES_COUNT 2
#endif

#ifndef SPI_HAS_TRANSACTION
#define SPI_HAS_TRANSACTION 1
#endif

typedef uint8_t BitOrder;

#ifndef LSBFIRST
#define LSBFIRST HAL_SPI_LSBFIRST
#endif
#ifndef MSBFIRST
#define MSBFIRST HAL_SPI_MSBFIRST
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 HAL_SPI_MODE0
#endif
#ifndef SPI_MODE1
#define SPI_MODE1 HAL_SPI_MODE1
#endif
#ifndef SPI_MODE2
#define SPI_MODE2 HAL_SPI_MODE2
#endif
#ifndef SPI_MODE3
#define SPI_MODE3 HAL_SPI_MODE3
#endif

class SPISettings {
public:
    SPISettings()
        : _clock(HAL_SPI_CLOCK_DEFAULT_HZ), _bitOrder(MSBFIRST), _dataMode(SPI_MODE0) {}
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode)
        : _clock(clock), _bitOrder(bitOrder), _dataMode(dataMode) {}

    hal_spi_settings_t toHalSettings() const {
        hal_spi_settings_t s = {
            _clock,
            (uint8_t)(_bitOrder == LSBFIRST ? HAL_SPI_LSBFIRST : HAL_SPI_MSBFIRST),
            (uint8_t)(_dataMode <= SPI_MODE3 ? _dataMode : SPI_MODE0),
        };
        return s;
    }

private:
    uint32_t _clock;
    uint8_t _bitOrder;
    uint8_t _dataMode;
};

class SPIClass {
public:
    explicit SPIClass(uint8_t bus = 0)
        : _bus(bus == 1 ? 1 : 0) {
        setDefaultPins();
    }

    void setRX(uint8_t pin) { _rx = pin; }
    void setMISO(uint8_t pin) { setRX(pin); }
    void setTX(uint8_t pin) { _tx = pin; }
    void setMOSI(uint8_t pin) { setTX(pin); }
    void setSCK(uint8_t pin) { _sck = pin; }

    void begin() { hal_spi_init(_bus, _rx, _tx, _sck); }
    void begin(bool) { begin(); }
    void end() { hal_spi_deinit(_bus); }

    void beginTransaction(SPISettings settings) {
        hal_spi_settings_t halSettings = settings.toHalSettings();
        hal_spi_begin_transaction(_bus, &halSettings);
    }

    void endTransaction() { hal_spi_end_transaction(_bus); }

    uint8_t transfer(uint8_t data) { return hal_spi_transfer(_bus, data); }
    uint16_t transfer16(uint16_t data) { return hal_spi_transfer16(_bus, data); }

    void transfer(uint8_t *buffer, size_t count) {
        hal_spi_transfer_buffer(_bus, buffer, count);
    }

    void transfer(void *buffer, size_t count) {
        hal_spi_transfer_buffer(_bus, static_cast<uint8_t *>(buffer), count);
    }

    void transfer(const void *txBuffer, void *rxBuffer, size_t count) {
        hal_spi_transfer_txrx(_bus,
                              static_cast<const uint8_t *>(txBuffer),
                              static_cast<uint8_t *>(rxBuffer),
                              count);
    }

    size_t write(uint8_t data) {
        hal_spi_write(_bus, &data, 1u);
        return 1u;
    }

    size_t write(const uint8_t *buffer, size_t count) {
        hal_spi_write(_bus, buffer, count);
        return count;
    }

    void write16(uint16_t data) { (void)transfer16(data); }
    void write32(uint32_t data) {
        write((uint8_t)(data >> 24));
        write((uint8_t)(data >> 16));
        write((uint8_t)(data >> 8));
        write((uint8_t)data);
    }

    void writeBytes(const uint8_t *buffer, size_t count) { (void)write(buffer, count); }
    void transferBytes(const uint8_t *txBuffer, uint8_t *rxBuffer, size_t count) {
        hal_spi_transfer_txrx(_bus, txBuffer, rxBuffer, count);
    }

    void setFrequency(uint32_t clock) {
        _settings = SPISettings(clock, MSBFIRST, SPI_MODE0);
        beginTransaction(_settings);
    }
    void setClock(uint32_t clock) { setFrequency(clock); }
    void setBitOrder(uint8_t bitOrder) {
        _settings = SPISettings(HAL_SPI_CLOCK_DEFAULT_HZ, bitOrder, SPI_MODE0);
        beginTransaction(_settings);
    }
    void setDataMode(uint8_t dataMode) {
        _settings = SPISettings(HAL_SPI_CLOCK_DEFAULT_HZ, MSBFIRST, dataMode);
        beginTransaction(_settings);
    }
    void setClockDivider(uint32_t) {}

    uint8_t bus() const { return _bus; }

private:
    void setDefaultPins() {
        if (_bus == 1u) {
            _rx = (uint8_t)(1u * 16u + 14u);  /* PB14 = SPI2_MISO */
            _tx = (uint8_t)(1u * 16u + 15u);  /* PB15 = SPI2_MOSI */
            _sck = (uint8_t)(1u * 16u + 13u); /* PB13 = SPI2_SCK  */
        } else {
            _rx = 6u;  /* PA6 = SPI1_MISO */
            _tx = 7u;  /* PA7 = SPI1_MOSI */
            _sck = 5u; /* PA5 = SPI1_SCK  */
        }
    }

    uint8_t _bus;
    uint8_t _rx;
    uint8_t _tx;
    uint8_t _sck;
    SPISettings _settings;
};

inline SPIClass SPI;
inline SPIClass SPI1(1);
