#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef bool boolean;

unsigned long millis(void);
void yield(void);

#define pgm_read_byte_near(address) (*(const uint8_t *)(address))
