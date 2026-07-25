#include "hal_crc.h"

#ifdef HAL_ENABLE_CRC

/*
 * Table-free reference implementations. Each routine is the standard bitwise
 * form of its catalog variant; correctness is pinned by the "123456789" check
 * values documented in hal_crc.h and exercised by tests/test_hal_crc.cpp.
 */

extern "C" {

uint8_t hal_crc8_maxim(const uint8_t *data, size_t len) {
  if (!data || len == 0u) {
    return 0u;
  }

  uint8_t crc = 0u;
  for (size_t i = 0u; i < len; ++i) {
    uint8_t in = data[i];
    for (uint8_t b = 0u; b < 8u; ++b) {
      const uint8_t mix = (uint8_t)((crc ^ in) & 0x01u);
      crc >>= 1;
      if (mix) {
        crc ^= 0x8Cu;
      }
      in >>= 1;
    }
  }
  return crc;
}

uint16_t hal_crc16_maxim(const uint8_t *data, size_t len, uint16_t crc) {
  static const uint8_t oddparity[16] = {0u, 1u, 1u, 0u, 1u, 0u, 0u, 1u,
                                        1u, 0u, 0u, 1u, 0u, 1u, 1u, 0u};

  if (!data) {
    return crc;
  }

  for (size_t i = 0u; i < len; ++i) {
    uint16_t cdata = data[i];
    cdata = (uint16_t)((cdata ^ crc) & 0xFFu);
    crc >>= 8u;

    if (oddparity[cdata & 0x0Fu] ^ oddparity[cdata >> 4u]) {
      crc ^= 0xC001u;
    }

    cdata <<= 6u;
    crc ^= cdata;
    cdata <<= 1u;
    crc ^= cdata;
  }

  return crc;
}

bool hal_crc16_maxim_check(const uint8_t *data, size_t len,
                           const uint8_t inverted_crc[2], uint16_t crc) {
  if (!data || !inverted_crc) {
    return false;
  }

  crc = (uint16_t)~hal_crc16_maxim(data, len, crc);
  return ((crc & 0xFFu) == inverted_crc[0]) && ((crc >> 8u) == inverted_crc[1]);
}

uint16_t hal_crc16_ccitt(const uint8_t *data, size_t len, uint16_t crc) {
  if (!data) {
    return crc;
  }

  for (size_t i = 0u; i < len; ++i) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8u);
    for (uint8_t b = 0u; b < 8u; ++b) {
      if (crc & 0x8000u) {
        crc = (uint16_t)((crc << 1u) ^ 0x1021u);
      } else {
        crc = (uint16_t)(crc << 1u);
      }
    }
  }

  return crc;
}

uint32_t hal_crc32(const uint8_t *data, size_t len) {
  if (!data || len == 0u) {
    return 0u;
  }

  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0u; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0u; b < 8u; ++b) {
      if (crc & 1u) {
        crc = (crc >> 1u) ^ 0xEDB88320u;
      } else {
        crc >>= 1u;
      }
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

} // extern "C"

#endif /* HAL_ENABLE_CRC */
