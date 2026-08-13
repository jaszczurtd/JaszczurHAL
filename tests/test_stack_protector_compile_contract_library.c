#if !defined(__SSP_STRONG__)
#error "The stack-protector usage requirement did not reach the library source"
#endif

#include <stddef.h>

int jh_test_stack_protector_copy(const char *input) {
  char local[16] = {0};
  size_t index = 0u;
  while (index + 1u < sizeof(local) && input[index] != '\0') {
    local[index] = input[index];
    ++index;
  }
  return (int)local[0];
}
