#pragma once
/*
 * Minimal Arduino.h stub for Arduino platform — host/test builds only.
 * Provides only the types that tools.h / tools.cpp require.
 * Does NOT define ARDUINO so hal_config.h uses the hosted-build paths.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <string>

/* String — minimal subset used in tools.h / tools.cpp function signatures */
class String {
    std::string _s;
public:
    String() = default;
    String(const char *s) : _s(s ? s : "") {}
    String(const String &) = default;
    String &operator=(const String &) = default;
    String operator+(const char *s) const { return String((_s + s).c_str()); }
    const char *c_str() const { return _s.c_str(); }
    size_t length() const { return _s.size(); }
    bool operator==(const char *s) const { return _s == s; }
};

/* Minimal runtime API stubs used by Arduino backend files during
 * host-side IntelliSense/diagnostics. These are no-op placeholders and
 * are not used in real firmware builds. */
inline void analogReadResolution(int) {}
inline int analogRead(int) { return 0; }
inline void analogWriteResolution(int) {}
inline void analogWrite(int, int) {}
