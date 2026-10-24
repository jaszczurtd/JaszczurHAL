#pragma once

#include "hal/core/hal_compiler.h"

static inline void __dmb(void) { HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_SEQ_CST); }
