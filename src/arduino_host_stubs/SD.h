#pragma once
/*
 * Minimal SD.h stub for Arduino platform - host/test builds only.
 * File, SD, and FILE_WRITE/FILE_READ are referenced by HAL_ENABLE_SDLOGGER
 * builds and by selected Arduino-compatible sources during host diagnostics.
 */
#include <stdint.h>

#define FILE_WRITE 1
#define FILE_READ  0

class File {
public:
    File() = default;
    explicit operator bool() const { return false; }
    template<typename T> void println(T) {}
    void flush() {}
    void close() {}
};

class SDClass {
public:
    bool begin(int) { return false; }
    File open(const char *, int = FILE_READ) { return {}; }
};

inline SDClass SD;
