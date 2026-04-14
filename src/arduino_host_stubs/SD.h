#pragma once
/*
 * Minimal SD.h stub for Arduino platform — host/test builds only.
 * File is referenced at file scope in tools.cpp (not inside #ifdef SD_LOGGER).
 * SD object and FILE_WRITE/FILE_READ are used only inside #ifdef SD_LOGGER
 * (dead code on test builds), but the definitions are harmless.
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
