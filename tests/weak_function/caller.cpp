#include "probe.h"

int call_from_cpp(void) {
  int value = 9;
  void (*function)(int *) = weak_cpp_update;
  function(&value);
  return (value == 9 + EXPECTED_OFFSET &&
          weak_c_value(13) == 13 + EXPECTED_OFFSET &&
          *weak_cpp_pointer() == EXPECTED_OFFSET)
             ? 0
             : 2;
}
