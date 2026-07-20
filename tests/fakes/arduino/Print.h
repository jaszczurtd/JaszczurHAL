#pragma once

#include <stddef.h>
#include <stdint.h>

namespace arduino {

class Print {
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t data) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t written = 0u;
    while (written < size && write(buffer[written]) == 1u) {
      ++written;
    }
    return written;
  }
};

} // namespace arduino

using arduino::Print;
