#include "hal/core/hal_compiler.h"
#include "probe.h"

HAL_WEAK_FUNCTION(const int *, weak_cpp_pointer, (void)) {
  static const int value = 10;
  return &value;
}

HAL_WEAK_FUNCTION(void, weak_cpp_update, (int *value)) { *value += 10; }

int call_from_cpp_default(int value) {
  weak_cpp_update(&value);
  return value + *weak_cpp_pointer();
}
