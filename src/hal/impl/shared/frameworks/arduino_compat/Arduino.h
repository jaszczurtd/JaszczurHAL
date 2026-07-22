#pragma once

#include "../../../../hal_net.h"
#include "../../../../hal_system.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef bool boolean;

static inline unsigned long millis(void) { return (unsigned long)hal_millis(); }

static inline void yield(void) {
  (void)hal_net_service();
  hal_idle();
}

#define pgm_read_byte_near(address) (*(const uint8_t *)(address))
