#include "hal/core/jh_endian.h"

uint8_t(MSB)(unsigned short value) { return jh_u16_msb(value); }

uint8_t(LSB)(unsigned short value) { return jh_u16_lsb(value); }
