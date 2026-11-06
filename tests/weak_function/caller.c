#include "probe.h"

int main(void) {
  int value = 3;
  int (*function)(int) = weak_c_value;
  weak_cpp_update(&value);
  if (value != 3 + EXPECTED_OFFSET || function(7) != 7 + EXPECTED_OFFSET ||
      *weak_cpp_pointer() != EXPECTED_OFFSET ||
      call_from_c_default(5) != 5 + EXPECTED_OFFSET ||
      call_from_cpp_default(11) != 11 + 2 * EXPECTED_OFFSET) {
    return 1;
  }
  return call_from_cpp();
}
