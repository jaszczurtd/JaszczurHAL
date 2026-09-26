#pragma once

#define __force_inline inline __attribute__((always_inline))
#define __no_inline_not_in_flash_func(name) name
#define __compiler_memory_barrier() __asm__ volatile("" ::: "memory")
