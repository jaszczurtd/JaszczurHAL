#pragma once
#include <stdint.h>
extern uint8_t test_rp_flash[4u * 1024u * 1024u];
#define XIP_BASE ((uintptr_t)test_rp_flash)
