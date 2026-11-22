#pragma once
#include <stddef.h>
#include <stdint.h>
int usb_serial_jtag_write_bytes(const void *data, size_t length,
                                uint32_t ticks_to_wait);
