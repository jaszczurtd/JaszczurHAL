#if !defined(__SSP_STRONG__)
#error                                                                         \
    "The stack-protector usage requirement did not reach the C++ consumer source"
#endif

extern "C" int jh_test_stack_protector_cpp(int seed) {
  volatile char local[16] = {0};
  local[0] = static_cast<char>(seed);
  return static_cast<int>(local[0]);
}
