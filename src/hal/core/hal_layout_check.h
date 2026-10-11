/* No include guard: each inclusion runs the check below once more. */

/**
 * @file hal_layout_check.h
 * @brief Stops a unit whose struct packing changes between HAL headers.
 *
 * Every HAL header that defines a structure or a union includes this file
 * before its own definitions. The first inclusion in a unit records the
 * packing in effect; each later one defines a fresh probe and compares it
 * with that record, so a `#pragma pack` opened between two HAL includes
 * stops the compilation at the next HAL header instead of silently changing
 * the layout of its types. The packing of the first HAL include is checked
 * against the library by the generated jh_board_config.h of a device tree
 * build. A structure packed on purpose uses HAL_PACKED inside the header.
 */

#ifndef JH_LAYOUT_CHECK_DEFINED
#define JH_LAYOUT_CHECK_DEFINED

#if !defined(__ASSEMBLER__)
#include <stddef.h>

/* Packing shows in the offset of the most aligned basic type. */
struct jh_layout_reference {
  char c;
  long double x;
};

#define JH_LAYOUT_JOIN_(a, b) a##b
#define JH_LAYOUT_JOIN(a, b) JH_LAYOUT_JOIN_(a, b)
#ifdef __cplusplus
#define JH_LAYOUT_ASSERT static_assert
#else
#define JH_LAYOUT_ASSERT _Static_assert
#endif

/**
 * @brief Compares the packing in effect with the first HAL include.
 *
 * @param id Unique token for the probe structure; the header passes
 *           `__COUNTER__`, so every inclusion defines its own probe.
 */
#define JH_LAYOUT_CHECK(id)                                                    \
  struct JH_LAYOUT_JOIN(jh_layout_probe_, id) {                                \
    char c;                                                                    \
    long double x;                                                             \
  };                                                                           \
  JH_LAYOUT_ASSERT(offsetof(struct JH_LAYOUT_JOIN(jh_layout_probe_, id), x) == \
                       offsetof(struct jh_layout_reference, x),                \
                   "struct packing changed since the first HAL header; close " \
                   "#pragma pack before including HAL headers")
#endif
#endif

#if !defined(__ASSEMBLER__)
JH_LAYOUT_CHECK(__COUNTER__);
#endif
