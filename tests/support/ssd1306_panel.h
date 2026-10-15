#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

/* SSD1306 RAM/address model for host tests. Commands and data arrive in their
 * actual I2C frames. Page selection in horizontal mode is deliberately
 * rejected. This models documented digital addressing, not panel voltages or
 * timing. */
class Ssd1306Panel {
public:
  std::array<uint8_t, 1024> ram{};
  std::array<uint8_t, 256> registers{};
  std::array<size_t, 256> commands{};
  bool enabled = false;
  size_t data_bytes = 0;

  void frame(const uint8_t *bytes, size_t length) {
    assert(length > 1);
    assert(bytes[0] == 0x00 || bytes[0] == 0x40);
    for (size_t i = 1; i < length; ++i) {
      if (bytes[0] == 0x00) {
        command(bytes[i]);
      } else {
        assert(pending == 0 && column < 128 && page < 8);
        ram[page * 128 + column] = bytes[i];
        ++data_bytes;
        if (mode == 2) {
          column = (column + 1) % 128;
        } else {
          assert(mode == 0);
          if (++column > column_end) {
            column = column_start;
            if (++page > page_end)
              page = page_start;
          }
        }
      }
    }
  }

private:
  uint8_t mode = 2, column = 0, page = 0;
  uint8_t column_start = 0, column_end = 127, page_start = 0, page_end = 7;
  uint8_t opcode = 0, pending = 0;

  void command(uint8_t byte) {
    if (pending) {
      --pending;
      registers[opcode] = byte;
      if (opcode == 0x20)
        mode = byte;
      if (opcode == 0x21) {
        if (pending)
          column = column_start = byte;
        else
          column_end = byte;
      }
      if (opcode == 0x22) {
        if (pending)
          page = page_start = byte;
        else
          page_end = byte;
      }
      return;
    }
    ++commands[byte];
    opcode = byte;
    switch (byte) {
    case 0x21:
    case 0x22:
      pending = 2;
      break;
    case 0x20:
    case 0x81:
    case 0x82:
    case 0x8d:
    case 0xa8:
    case 0xad:
    case 0xd3:
    case 0xd5:
    case 0xd9:
    case 0xda:
    case 0xdb:
      pending = 1;
      break;
    case 0xae:
      enabled = false;
      break;
    case 0xaf:
      enabled = true;
      break;
    default:
      if (byte <= 0x0f) {
        assert(mode == 2);
        column = (column & 0xf0) | byte;
      } else if (byte >= 0x10 && byte <= 0x1f) {
        assert(mode == 2);
        column = (column & 0x0f) | ((byte & 0x0f) << 4);
      } else if (byte >= 0xb0 && byte <= 0xb7) {
        assert(mode == 2);
        page = byte & 7;
      }
      break;
    }
  }
};
