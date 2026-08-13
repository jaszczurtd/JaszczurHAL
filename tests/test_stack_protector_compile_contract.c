#include "hal/system/jh_stack_protector.h"

#if !defined(__SSP_STRONG__)
#error "The stack-protector usage requirement did not reach the consumer source"
#endif

int jh_test_stack_protector_copy(const char *input);
int jh_test_stack_protector_cpp(int seed);

HAL_NORETURN HAL_NO_STACK_PROTECTOR void
jh_stack_overflow_reset_with_context(uintptr_t pc, uintptr_t lr) {
  (void)pc;
  (void)lr;
  for (;;) {
  }
}

HAL_NORETURN HAL_NO_STACK_PROTECTOR void jh_stack_overflow_reset(void) {
  jh_stack_overflow_reset_with_context(0u, 0u);
}

int main(void) {
  if (__stack_chk_guard == (uintptr_t)0u) {
    return 1;
  }
  if (jh_test_stack_protector_copy("A") != (int)'A') {
    return 2;
  }
  return jh_test_stack_protector_cpp((int)'B') == (int)'B' ? 0 : 3;
}
