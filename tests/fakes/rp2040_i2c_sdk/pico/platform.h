#pragma once

/* Host tests pick the core each thread represents. */
extern thread_local unsigned int jh_test_core_num;
inline unsigned int get_core_num(void) { return jh_test_core_num; }
