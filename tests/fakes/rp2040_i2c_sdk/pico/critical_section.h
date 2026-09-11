#pragma once
struct critical_section_t {
  unsigned int depth;
};
extern unsigned int jh_test_i2c_lock_depth;
inline void critical_section_init(critical_section_t *lock) {
  lock->depth = 0U;
}
inline void critical_section_enter_blocking(critical_section_t *lock) {
  ++lock->depth;
  ++jh_test_i2c_lock_depth;
}
inline void critical_section_exit(critical_section_t *lock) {
  --lock->depth;
  --jh_test_i2c_lock_depth;
}
