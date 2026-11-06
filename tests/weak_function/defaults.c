#include "hal/core/hal_compiler.h"
#include "probe.h"

HAL_WEAK_FUNCTION(int, weak_c_value, (int value)) { return value + 10; }

/* This ordinary symbol also extracts the default's object from the archive. */
int call_from_c_default(int value) { return weak_c_value(value); }
