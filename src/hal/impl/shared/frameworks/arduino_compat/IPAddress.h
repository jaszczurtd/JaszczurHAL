#pragma once

#include <stdint.h>

namespace arduino {

class IPAddress {
public:
  IPAddress() : bytes_{} {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d} {}
  explicit IPAddress(const uint8_t *address)
      : bytes_{address[0], address[1], address[2], address[3]} {}

  uint8_t operator[](int index) const { return bytes_[index]; }
  uint8_t &operator[](int index) { return bytes_[index]; }

  bool operator==(const IPAddress &other) const {
    return bytes_[0] == other.bytes_[0] && bytes_[1] == other.bytes_[1] &&
           bytes_[2] == other.bytes_[2] && bytes_[3] == other.bytes_[3];
  }

private:
  uint8_t bytes_[4];
};

} // namespace arduino

using arduino::IPAddress;
