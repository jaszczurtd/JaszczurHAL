#include "probe.h"

const int *weak_cpp_pointer(void) {
  static const int value = 100;
  return &value;
}

void weak_cpp_update(int *value) { *value += 100; }
