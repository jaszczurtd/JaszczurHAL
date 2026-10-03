#pragma once
#include <stdint.h>
inline void busy_wait_us(uint64_t) {}
using absolute_time_t = uint64_t;
inline bool jh_test_i2c_timeout = false;
inline absolute_time_t make_timeout_time_us(uint64_t us) { return us; }
inline bool time_reached(absolute_time_t) { return jh_test_i2c_timeout; }
