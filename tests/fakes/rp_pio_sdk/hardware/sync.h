#pragma once

static inline void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
