#pragma once

/**
 * @file hal_compiler.h
 * @brief One source for the compiler extensions JaszczurHAL relies on.
 *
 * Firmware always builds with GNU toolchains, while host targets also build
 * with Clang and MSVC. Every compiler-specific attribute or builtin used by
 * JaszczurHAL code belongs here, so a new host compiler is a change in one
 * file instead of a sweep through the tree.
 *
 * Scope rules:
 * - JaszczurHAL code uses these macros, including the port headers it owns;
 * - vendored third-party sources keep their upstream form, because matching
 *   upstream simplifies updates and those sources never see a second compiler;
 * - linker-level attributes (@c section, @c naked, @c constructor) and inline
 *   assembly stay explicit at their target-specific call sites, where a wrong
 *   mapping would silently corrupt the memory map.
 *
 * The header is standalone: it pulls in nothing from the rest of the HAL, so
 * runtime, port and test translation units can include it directly.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Both identities may be pre-defined to 0 to force the portable fallback.
 * Tests use that to build a branch no real compiler selects; an exotic port
 * can use it before its own mapping exists. */
#ifndef HAL_COMPILER_IS_MSVC
#if defined(_MSC_VER) && !defined(__clang__)
#define HAL_COMPILER_IS_MSVC 1
#else
#define HAL_COMPILER_IS_MSVC 0
#endif
#endif

#ifndef HAL_COMPILER_IS_GNU_LIKE
#if defined(__GNUC__) || defined(__clang__)
#define HAL_COMPILER_IS_GNU_LIKE 1
#else
#define HAL_COMPILER_IS_GNU_LIKE 0
#endif
#endif

#if HAL_COMPILER_IS_MSVC || !HAL_COMPILER_IS_GNU_LIKE
#include <stdlib.h>
#endif
#if HAL_COMPILER_IS_MSVC
#include <intrin.h>
#include <string.h>
#endif

/** @def HAL_NORETURN
 *  @brief Declares a function that never returns.
 *  @note Place it after the storage class and before the return type
 *  (@c static @c HAL_NORETURN @c void @c f(void)); MSVC rejects some other
 *  positions for @c __declspec.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_NORETURN __attribute__((noreturn))
#elif HAL_COMPILER_IS_MSVC
#define HAL_NORETURN __declspec(noreturn)
#else
#define HAL_NORETURN
#endif

/** @def HAL_NO_STACK_PROTECTOR
 *  @brief Excludes a function from compiler stack-canary instrumentation.
 *
 *  Use only in the stack-protector runtime and its terminal fault/reset path.
 *  Those functions must remain callable after the compiler has detected a
 *  damaged stack frame and must not recursively depend on the same canary.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_NO_STACK_PROTECTOR __attribute__((no_stack_protector))
#else
#define HAL_NO_STACK_PROTECTOR
#endif

/** @def HAL_FORCE_INLINE
 *  @brief Requests inlining regardless of the optimizer's own decision.
 *
 *  The macro carries the inline specifier itself. Writing @c inline next to it
 *  produces a duplicate specifier on GNU and warning C4141 on MSVC, so use it
 *  alone: @c static @c HAL_FORCE_INLINE @c uint32_t @c f(void).
 *  @note Only for timing-critical target code that documents why it needs it.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_FORCE_INLINE inline __attribute__((always_inline))
#elif HAL_COMPILER_IS_MSVC
#define HAL_FORCE_INLINE __forceinline
#else
#define HAL_FORCE_INLINE inline
#endif

/** @def HAL_TRAP
 *  @brief Stops execution immediately at an unrecoverable point.
 *
 *  GNU builds emit a trapping instruction. MSVC breaks into the debugger and
 *  then aborts, because @c __debugbreak() alone returns to the caller.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_TRAP() __builtin_trap()
#elif HAL_COMPILER_IS_MSVC
#define HAL_TRAP()                                                             \
  do {                                                                         \
    __debugbreak();                                                            \
    abort();                                                                   \
  } while (0)
#else
#define HAL_TRAP() abort()
#endif

/** @def HAL_UNREACHABLE
 *  @brief Marks a path the program must never take.
 *  @note Reaching it is undefined behaviour on GNU and MSVC; use @c HAL_TRAP()
 *  when the condition can occur at runtime and must be diagnosable.
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_UNREACHABLE() __builtin_unreachable()
#elif HAL_COMPILER_IS_MSVC
#define HAL_UNREACHABLE() __assume(0)
#else
#define HAL_UNREACHABLE() HAL_TRAP()
#endif

/** @def HAL_PACKED
 *  @brief Removes padding from a structure definition.
 *
 *  MSVC has no packing attribute, so the pragma pair is part of the contract.
 *  Wrap every packed definition:
 *  @code
 *  HAL_PACKED_BEGIN
 *  struct wire_header {
 *    uint8_t kind;
 *    uint32_t length;
 *  } HAL_PACKED;
 *  HAL_PACKED_END
 *  @endcode
 */
#if HAL_COMPILER_IS_GNU_LIKE
#define HAL_PACKED __attribute__((__packed__))
#define HAL_PACKED_BEGIN
#define HAL_PACKED_END
#elif HAL_COMPILER_IS_MSVC
#define HAL_PACKED
#define HAL_PACKED_BEGIN __pragma(pack(push, 1))
#define HAL_PACKED_END __pragma(pack(pop))
#else
#define HAL_PACKED
#define HAL_PACKED_BEGIN
#define HAL_PACKED_END
#endif

/**
 * @brief Memory ordering used by the HAL atomic operations.
 *
 * Pass the weakest ordering that preserves the synchronization required by
 * the caller. Load, store and compare-exchange operations accept only the
 * orderings allowed by the C11 memory model. MSVC host builds may strengthen a
 * requested ordering because their Interlocked intrinsics provide full memory
 * barriers.
 */
typedef enum {
#if HAL_COMPILER_IS_GNU_LIKE
  HAL_ATOMIC_RELAXED = __ATOMIC_RELAXED,
  HAL_ATOMIC_ACQUIRE = __ATOMIC_ACQUIRE,
  HAL_ATOMIC_RELEASE = __ATOMIC_RELEASE,
  HAL_ATOMIC_ACQ_REL = __ATOMIC_ACQ_REL,
  HAL_ATOMIC_SEQ_CST = __ATOMIC_SEQ_CST,
#else
  HAL_ATOMIC_RELAXED = 0,
  HAL_ATOMIC_ACQUIRE = 2,
  HAL_ATOMIC_RELEASE = 3,
  HAL_ATOMIC_ACQ_REL = 4,
  HAL_ATOMIC_SEQ_CST = 5,
#endif
} hal_atomic_memory_order_t;

/** @def HAL_ATOMIC_LOAD
 *  @brief Atomically load a scalar value.
 *  @param object Pointer to the value to load.
 *  @param order `HAL_ATOMIC_RELAXED`, `HAL_ATOMIC_ACQUIRE`, or
 *               `HAL_ATOMIC_SEQ_CST`.
 *  @return The loaded value, with the same type as `*object`.
 */

/** @def HAL_ATOMIC_POINTER_LOAD
 *  @brief Atomically load an object pointer.
 *  @param object Pointer to the pointer value to load.
 *  @param order `HAL_ATOMIC_RELAXED`, `HAL_ATOMIC_ACQUIRE`, or
 *               `HAL_ATOMIC_SEQ_CST`.
 *  @return The loaded pointer, with the same type as `*object`.
 */

/** @def HAL_ATOMIC_STORE
 *  @brief Atomically store a scalar or pointer value.
 *  @param object Pointer to the destination.
 *  @param value Value to store.
 *  @param order `HAL_ATOMIC_RELAXED`, `HAL_ATOMIC_RELEASE`, or
 *               `HAL_ATOMIC_SEQ_CST`.
 */

/** @def HAL_ATOMIC_EXCHANGE
 *  @brief Atomically replace a value and return its previous value.
 *  @param object Pointer to the destination.
 *  @param value Replacement value.
 *  @param order Memory ordering for the read-modify-write operation.
 *  @return Value stored in `*object` before the exchange.
 */

/** @def HAL_ATOMIC_COMPARE_EXCHANGE
 *  @brief Perform a strong atomic compare-exchange operation.
 *  @param object Pointer to the destination.
 *  @param expected Pointer to the expected value. On failure it receives the
 *                  value observed in `*object`.
 *  @param desired Value stored when `*object` equals `*expected`.
 *  @param success_order Memory ordering used on success.
 *  @param failure_order Memory ordering used on failure. Release and
 *                       acquire-release are invalid failure orderings.
 *  @return `true` when the exchange succeeds; otherwise `false`.
 */

/** @def HAL_ATOMIC_POINTER_COMPARE_EXCHANGE
 *  @brief Perform a strong atomic compare-exchange on an object pointer.
 *  @param object Pointer to the pointer value to update.
 *  @param expected Pointer to the expected pointer. On failure it receives the
 *                  pointer observed in `*object`.
 *  @param desired Pointer stored when `*object` equals `*expected`.
 *  @param success_order Memory ordering used on success.
 *  @param failure_order Memory ordering used on failure. Release and
 *                       acquire-release are invalid failure orderings.
 *  @return `true` when the exchange succeeds; otherwise `false`.
 */

/** @def HAL_ATOMIC_FETCH_ADD
 *  @brief Atomically add and return the value before the addition.
 *  @param object Pointer to the integer value.
 *  @param value Value to add.
 *  @param order Memory ordering for the read-modify-write operation.
 *  @return Value stored in `*object` before the addition.
 */

/** @def HAL_ATOMIC_FETCH_SUB
 *  @brief Atomically subtract and return the value before subtraction.
 *  @param object Pointer to the integer value.
 *  @param value Value to subtract.
 *  @param order Memory ordering for the read-modify-write operation.
 *  @return Value stored in `*object` before subtraction.
 */

/** @def HAL_ATOMIC_FETCH_OR
 *  @brief Atomically apply bitwise OR and return the previous value.
 *  @param object Pointer to the integer value.
 *  @param value Bit mask to apply.
 *  @param order Memory ordering for the read-modify-write operation.
 *  @return Value stored in `*object` before the update.
 */

/** @def HAL_ATOMIC_ADD_FETCH
 *  @brief Atomically add and return the resulting value.
 *  @param object Pointer to the integer value.
 *  @param value Value to add.
 *  @param order Memory ordering for the read-modify-write operation.
 *  @return Value stored in `*object` after the addition.
 */

/** @def HAL_ATOMIC_SUB_FETCH
 *  @brief Atomically subtract and return the resulting value.
 *  @param object Pointer to the integer value.
 *  @param value Value to subtract.
 *  @param order Memory ordering for the read-modify-write operation.
 *  @return Value stored in `*object` after subtraction.
 */

/** @def HAL_ATOMIC_TEST_AND_SET
 *  @brief Atomically set a `bool` or byte flag.
 *  @param object Pointer to the flag.
 *  @param order Memory ordering for the read-modify-write operation.
 *  @return `true` when the flag was already set; otherwise `false`.
 */

/** @def HAL_ATOMIC_CLEAR
 *  @brief Atomically clear a `bool` or byte flag.
 *  @param object Pointer to the flag.
 *  @param order `HAL_ATOMIC_RELAXED`, `HAL_ATOMIC_RELEASE`, or
 *               `HAL_ATOMIC_SEQ_CST`.
 */

/** @def HAL_ATOMIC_THREAD_FENCE
 *  @brief Apply a thread fence with the requested memory ordering.
 *  @param order Memory ordering for the fence.
 */

#if HAL_COMPILER_IS_GNU_LIKE

#define HAL_ATOMIC_LOAD(object, order) __atomic_load_n((object), (order))
#define HAL_ATOMIC_STORE(object, value, order)                                 \
  __atomic_store_n((object), (value), (order))
#define HAL_ATOMIC_EXCHANGE(object, value, order)                              \
  __atomic_exchange_n((object), (value), (order))
#define HAL_ATOMIC_COMPARE_EXCHANGE(object, expected, desired, success_order,  \
                                    failure_order)                             \
  __atomic_compare_exchange_n((object), (expected), (desired), false,          \
                              (success_order), (failure_order))
#define HAL_ATOMIC_FETCH_ADD(object, value, order)                             \
  __atomic_fetch_add((object), (value), (order))
#define HAL_ATOMIC_FETCH_SUB(object, value, order)                             \
  __atomic_fetch_sub((object), (value), (order))
#define HAL_ATOMIC_FETCH_OR(object, value, order)                              \
  __atomic_fetch_or((object), (value), (order))
#define HAL_ATOMIC_ADD_FETCH(object, value, order)                             \
  __atomic_add_fetch((object), (value), (order))
#define HAL_ATOMIC_SUB_FETCH(object, value, order)                             \
  __atomic_sub_fetch((object), (value), (order))
#define HAL_ATOMIC_TEST_AND_SET(object, order)                                 \
  __atomic_test_and_set((object), (order))
#define HAL_ATOMIC_CLEAR(object, order) __atomic_clear((object), (order))
#define HAL_ATOMIC_THREAD_FENCE(order) __atomic_thread_fence((order))

#define HAL_ATOMIC_POINTER_LOAD(object, order)                                 \
  HAL_ATOMIC_LOAD((object), (order))
#define HAL_ATOMIC_POINTER_COMPARE_EXCHANGE(object, expected, desired,         \
                                            success_order, failure_order)      \
  HAL_ATOMIC_COMPARE_EXCHANGE((object), (expected), (desired),                 \
                              (success_order), (failure_order))

#elif HAL_COMPILER_IS_MSVC

static HAL_FORCE_INLINE bool
jh_atomic_msvc_rmw_order_valid(hal_atomic_memory_order_t order) {
  return order == HAL_ATOMIC_RELAXED || order == HAL_ATOMIC_ACQUIRE ||
         order == HAL_ATOMIC_RELEASE || order == HAL_ATOMIC_ACQ_REL ||
         order == HAL_ATOMIC_SEQ_CST;
}

static HAL_FORCE_INLINE bool
jh_atomic_msvc_load_order_valid(hal_atomic_memory_order_t order) {
  return order == HAL_ATOMIC_RELAXED || order == HAL_ATOMIC_ACQUIRE ||
         order == HAL_ATOMIC_SEQ_CST;
}

static HAL_FORCE_INLINE bool
jh_atomic_msvc_store_order_valid(hal_atomic_memory_order_t order) {
  return order == HAL_ATOMIC_RELAXED || order == HAL_ATOMIC_RELEASE ||
         order == HAL_ATOMIC_SEQ_CST;
}

static HAL_FORCE_INLINE bool
jh_atomic_msvc_compare_orders_valid(hal_atomic_memory_order_t success_order,
                                    hal_atomic_memory_order_t failure_order) {
  if (!jh_atomic_msvc_rmw_order_valid(success_order) ||
      !jh_atomic_msvc_load_order_valid(failure_order)) {
    return false;
  }
  if (failure_order == HAL_ATOMIC_SEQ_CST) {
    return success_order == HAL_ATOMIC_SEQ_CST;
  }
  if (failure_order == HAL_ATOMIC_ACQUIRE) {
    return success_order == HAL_ATOMIC_ACQUIRE ||
           success_order == HAL_ATOMIC_ACQ_REL ||
           success_order == HAL_ATOMIC_SEQ_CST;
  }
  return true;
}

static HAL_FORCE_INLINE uint64_t jh_atomic_msvc_mask(size_t size) {
  switch (size) {
  case 1u:
    return UINT8_MAX;
  case 2u:
    return UINT16_MAX;
  case 4u:
    return UINT32_MAX;
  case 8u:
    return UINT64_MAX;
  default:
    HAL_TRAP();
    return 0u;
  }
}

static HAL_FORCE_INLINE uint64_t jh_atomic_msvc_compare_exchange_bits(
    volatile void *object, uint64_t desired, uint64_t expected, size_t size) {
  switch (size) {
  case 1u:
    return (uint64_t)(uint8_t)_InterlockedCompareExchange8(
        (volatile char *)object, (char)desired, (char)expected);
  case 2u:
    return (uint64_t)(uint16_t)_InterlockedCompareExchange16(
        (volatile short *)object, (short)desired, (short)expected);
  case 4u:
    return (uint64_t)(uint32_t)_InterlockedCompareExchange(
        (volatile long *)object, (long)desired, (long)expected);
  case 8u:
    return (uint64_t)_InterlockedCompareExchange64(
        (volatile __int64 *)object, (__int64)desired, (__int64)expected);
  default:
    HAL_TRAP();
    return 0u;
  }
}

static HAL_FORCE_INLINE uint64_t jh_atomic_msvc_load_bits(
    const volatile void *object, size_t size, hal_atomic_memory_order_t order) {
  if (!jh_atomic_msvc_load_order_valid(order)) {
    HAL_TRAP();
  }
  return jh_atomic_msvc_compare_exchange_bits((volatile void *)object, 0u, 0u,
                                              size);
}

static HAL_FORCE_INLINE bool jh_atomic_msvc_compare_exchange_bits_strong(
    volatile void *object, uint64_t *expected, uint64_t desired, size_t size,
    hal_atomic_memory_order_t success_order,
    hal_atomic_memory_order_t failure_order) {
  if (!jh_atomic_msvc_compare_orders_valid(success_order, failure_order)) {
    HAL_TRAP();
  }
  const uint64_t mask = jh_atomic_msvc_mask(size);
  const uint64_t wanted = *expected & mask;
  const uint64_t observed =
      jh_atomic_msvc_compare_exchange_bits(object, desired, wanted, size) &
      mask;
  if (observed == wanted) {
    return true;
  }
  *expected = observed;
  return false;
}

static HAL_FORCE_INLINE uint64_t
jh_atomic_msvc_exchange_bits(volatile void *object, uint64_t desired,
                             size_t size, hal_atomic_memory_order_t order) {
  if (!jh_atomic_msvc_rmw_order_valid(order)) {
    HAL_TRAP();
  }
  uint64_t observed =
      jh_atomic_msvc_load_bits(object, size, HAL_ATOMIC_RELAXED);
  for (;;) {
    uint64_t expected = observed;
    if (jh_atomic_msvc_compare_exchange_bits_strong(
            object, &expected, desired, size, order, HAL_ATOMIC_RELAXED)) {
      return observed;
    }
    observed = expected;
  }
}

static HAL_FORCE_INLINE void
jh_atomic_msvc_store_bits(volatile void *object, uint64_t desired, size_t size,
                          hal_atomic_memory_order_t order) {
  if (!jh_atomic_msvc_store_order_valid(order)) {
    HAL_TRAP();
  }
  (void)jh_atomic_msvc_exchange_bits(object, desired, size, HAL_ATOMIC_SEQ_CST);
}

static HAL_FORCE_INLINE uint64_t jh_atomic_msvc_fetch_update_bits(
    volatile void *object, uint64_t value, size_t size,
    hal_atomic_memory_order_t order, bool subtract, bool bitwise_or) {
  if (!jh_atomic_msvc_rmw_order_valid(order)) {
    HAL_TRAP();
  }
  const uint64_t mask = jh_atomic_msvc_mask(size);
  uint64_t observed =
      jh_atomic_msvc_load_bits(object, size, HAL_ATOMIC_RELAXED);
  for (;;) {
    uint64_t desired =
        bitwise_or ? (observed | value)
                   : (subtract ? (observed - value) : (observed + value));
    desired &= mask;
    uint64_t expected = observed;
    if (jh_atomic_msvc_compare_exchange_bits_strong(
            object, &expected, desired, size, order, HAL_ATOMIC_RELAXED)) {
      return observed;
    }
    observed = expected;
  }
}

static HAL_FORCE_INLINE bool
jh_atomic_msvc_test_and_set(volatile void *object, size_t size,
                            hal_atomic_memory_order_t order) {
  if (size != 1u) {
    HAL_TRAP();
  }
  return jh_atomic_msvc_exchange_bits(object, 1u, size, order) != 0u;
}

static HAL_FORCE_INLINE void
jh_atomic_msvc_clear(volatile void *object, size_t size,
                     hal_atomic_memory_order_t order) {
  if (size != 1u || !jh_atomic_msvc_store_order_valid(order)) {
    HAL_TRAP();
  }
  jh_atomic_msvc_store_bits(object, 0u, size, order);
}

static HAL_FORCE_INLINE void
jh_atomic_msvc_thread_fence(hal_atomic_memory_order_t order) {
  if (!jh_atomic_msvc_rmw_order_valid(order)) {
    HAL_TRAP();
  }
  if (order != HAL_ATOMIC_RELAXED) {
    volatile long fence_word = 0;
    (void)_InterlockedCompareExchange(&fence_word, 0, 0);
  }
}

#ifdef __cplusplus

template <typename T>
static HAL_FORCE_INLINE uint64_t jh_atomic_msvc_to_bits(T value) {
  static_assert(sizeof(T) == 1u || sizeof(T) == 2u || sizeof(T) == 4u ||
                    sizeof(T) == 8u,
                "HAL atomics require a 1, 2, 4, or 8 byte value");
  uint64_t bits = 0u;
  memcpy(&bits, &value, sizeof(value));
  return bits;
}

template <typename T>
static HAL_FORCE_INLINE T jh_atomic_msvc_from_bits(uint64_t bits) {
  T value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

template <typename T>
static HAL_FORCE_INLINE T jh_atomic_msvc_load(const volatile T *object,
                                              hal_atomic_memory_order_t order) {
  return jh_atomic_msvc_from_bits<T>(
      jh_atomic_msvc_load_bits(object, sizeof(T), order));
}

template <typename T, typename U>
static HAL_FORCE_INLINE void
jh_atomic_msvc_store(volatile T *object, U value,
                     hal_atomic_memory_order_t order) {
  const T typed_value = (T)value;
  jh_atomic_msvc_store_bits(object, jh_atomic_msvc_to_bits(typed_value),
                            sizeof(T), order);
}

template <typename T, typename U>
static HAL_FORCE_INLINE T jh_atomic_msvc_exchange(
    volatile T *object, U value, hal_atomic_memory_order_t order) {
  const T typed_value = (T)value;
  return jh_atomic_msvc_from_bits<T>(jh_atomic_msvc_exchange_bits(
      object, jh_atomic_msvc_to_bits(typed_value), sizeof(T), order));
}

template <typename T, typename U>
static HAL_FORCE_INLINE bool
jh_atomic_msvc_compare_exchange(volatile T *object, T *expected, U desired,
                                hal_atomic_memory_order_t success_order,
                                hal_atomic_memory_order_t failure_order) {
  uint64_t expected_bits = jh_atomic_msvc_to_bits(*expected);
  const T typed_desired = (T)desired;
  const bool exchanged = jh_atomic_msvc_compare_exchange_bits_strong(
      object, &expected_bits, jh_atomic_msvc_to_bits(typed_desired), sizeof(T),
      success_order, failure_order);
  if (!exchanged) {
    *expected = jh_atomic_msvc_from_bits<T>(expected_bits);
  }
  return exchanged;
}

template <typename T, typename U>
static HAL_FORCE_INLINE T jh_atomic_msvc_fetch_update(
    volatile T *object, U value, hal_atomic_memory_order_t order, bool subtract,
    bool bitwise_or) {
  const T typed_value = (T)value;
  return jh_atomic_msvc_from_bits<T>(jh_atomic_msvc_fetch_update_bits(
      object, jh_atomic_msvc_to_bits(typed_value), sizeof(T), order, subtract,
      bitwise_or));
}

template <typename T, typename U>
static HAL_FORCE_INLINE T
jh_atomic_msvc_update_fetch(volatile T *object, U value,
                            hal_atomic_memory_order_t order, bool subtract) {
  const T typed_value = (T)value;
  const uint64_t value_bits = jh_atomic_msvc_to_bits(typed_value);
  const uint64_t previous_bits = jh_atomic_msvc_fetch_update_bits(
      object, value_bits, sizeof(T), order, subtract, false);
  const uint64_t result_bits =
      subtract ? (previous_bits - value_bits) : (previous_bits + value_bits);
  return jh_atomic_msvc_from_bits<T>(result_bits &
                                     jh_atomic_msvc_mask(sizeof(T)));
}

#define HAL_ATOMIC_LOAD(object, order) jh_atomic_msvc_load((object), (order))
#define HAL_ATOMIC_STORE(object, value, order)                                 \
  jh_atomic_msvc_store((object), (value), (order))
#define HAL_ATOMIC_EXCHANGE(object, value, order)                              \
  jh_atomic_msvc_exchange((object), (value), (order))
#define HAL_ATOMIC_COMPARE_EXCHANGE(object, expected, desired, success_order,  \
                                    failure_order)                             \
  jh_atomic_msvc_compare_exchange((object), (expected), (desired),             \
                                  (success_order), (failure_order))
#define HAL_ATOMIC_FETCH_ADD(object, value, order)                             \
  jh_atomic_msvc_fetch_update((object), (value), (order), false, false)
#define HAL_ATOMIC_FETCH_SUB(object, value, order)                             \
  jh_atomic_msvc_fetch_update((object), (value), (order), true, false)
#define HAL_ATOMIC_FETCH_OR(object, value, order)                              \
  jh_atomic_msvc_fetch_update((object), (value), (order), false, true)
#define HAL_ATOMIC_ADD_FETCH(object, value, order)                             \
  jh_atomic_msvc_update_fetch((object), (value), (order), false)
#define HAL_ATOMIC_SUB_FETCH(object, value, order)                             \
  jh_atomic_msvc_update_fetch((object), (value), (order), true)

#else

#define JH_DEFINE_MSVC_ATOMIC_SCALAR(suffix, type)                             \
  static HAL_FORCE_INLINE type jh_atomic_msvc_load_##suffix(                   \
      const volatile void *object, hal_atomic_memory_order_t order) {          \
    return (type)jh_atomic_msvc_load_bits(object, sizeof(type), order);        \
  }                                                                            \
  static HAL_FORCE_INLINE void jh_atomic_msvc_store_##suffix(                  \
      volatile void *object, type value, hal_atomic_memory_order_t order) {    \
    jh_atomic_msvc_store_bits(object, (uint64_t)value, sizeof(type), order);   \
  }                                                                            \
  static HAL_FORCE_INLINE type jh_atomic_msvc_exchange_##suffix(               \
      volatile void *object, type value, hal_atomic_memory_order_t order) {    \
    return (type)jh_atomic_msvc_exchange_bits(object, (uint64_t)value,         \
                                              sizeof(type), order);            \
  }                                                                            \
  static HAL_FORCE_INLINE bool jh_atomic_msvc_compare_exchange_##suffix(       \
      volatile void *object, void *expected, type desired,                     \
      hal_atomic_memory_order_t success_order,                                 \
      hal_atomic_memory_order_t failure_order) {                               \
    type *const typed_expected = (type *)expected;                             \
    uint64_t expected_bits = (uint64_t) * typed_expected;                      \
    const bool exchanged = jh_atomic_msvc_compare_exchange_bits_strong(        \
        object, &expected_bits, (uint64_t)desired, sizeof(type),               \
        success_order, failure_order);                                         \
    if (!exchanged) {                                                          \
      *typed_expected = (type)expected_bits;                                   \
    }                                                                          \
    return exchanged;                                                          \
  }                                                                            \
  static HAL_FORCE_INLINE type jh_atomic_msvc_fetch_update_##suffix(           \
      volatile void *object, type value, hal_atomic_memory_order_t order,      \
      bool subtract, bool bitwise_or) {                                        \
    return (type)jh_atomic_msvc_fetch_update_bits(                             \
        object, (uint64_t)value, sizeof(type), order, subtract, bitwise_or);   \
  }                                                                            \
  static HAL_FORCE_INLINE type jh_atomic_msvc_update_fetch_##suffix(           \
      volatile void *object, type value, hal_atomic_memory_order_t order,      \
      bool subtract) {                                                         \
    const uint64_t value_bits = (uint64_t)value;                               \
    const uint64_t previous_bits = jh_atomic_msvc_fetch_update_bits(           \
        object, value_bits, sizeof(type), order, subtract, false);             \
    const uint64_t result_bits = subtract ? (previous_bits - value_bits)       \
                                          : (previous_bits + value_bits);      \
    return (type)(result_bits & jh_atomic_msvc_mask(sizeof(type)));            \
  }

JH_DEFINE_MSVC_ATOMIC_SCALAR(bool, bool)
JH_DEFINE_MSVC_ATOMIC_SCALAR(char, char)
JH_DEFINE_MSVC_ATOMIC_SCALAR(schar, signed char)
JH_DEFINE_MSVC_ATOMIC_SCALAR(uchar, unsigned char)
JH_DEFINE_MSVC_ATOMIC_SCALAR(short, short)
JH_DEFINE_MSVC_ATOMIC_SCALAR(ushort, unsigned short)
JH_DEFINE_MSVC_ATOMIC_SCALAR(int, int)
JH_DEFINE_MSVC_ATOMIC_SCALAR(uint, unsigned int)
JH_DEFINE_MSVC_ATOMIC_SCALAR(long, long)
JH_DEFINE_MSVC_ATOMIC_SCALAR(ulong, unsigned long)
JH_DEFINE_MSVC_ATOMIC_SCALAR(llong, long long)
JH_DEFINE_MSVC_ATOMIC_SCALAR(ullong, unsigned long long)

#undef JH_DEFINE_MSVC_ATOMIC_SCALAR

#define JH_ATOMIC_MSVC_SCALAR_SELECT(object, operation)                        \
  _Generic(*(object),                                                          \
      bool: jh_atomic_msvc_##operation##_bool,                                 \
      char: jh_atomic_msvc_##operation##_char,                                 \
      signed char: jh_atomic_msvc_##operation##_schar,                         \
      unsigned char: jh_atomic_msvc_##operation##_uchar,                       \
      short: jh_atomic_msvc_##operation##_short,                               \
      unsigned short: jh_atomic_msvc_##operation##_ushort,                     \
      int: jh_atomic_msvc_##operation##_int,                                   \
      unsigned int: jh_atomic_msvc_##operation##_uint,                         \
      long: jh_atomic_msvc_##operation##_long,                                 \
      unsigned long: jh_atomic_msvc_##operation##_ulong,                       \
      long long: jh_atomic_msvc_##operation##_llong,                           \
      unsigned long long: jh_atomic_msvc_##operation##_ullong)

#define HAL_ATOMIC_LOAD(object, order)                                         \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), load)((object), (order))
#define HAL_ATOMIC_STORE(object, value, order)                                 \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), store)((object), (value), (order))
#define HAL_ATOMIC_EXCHANGE(object, value, order)                              \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), exchange)((object), (value), (order))
#define HAL_ATOMIC_COMPARE_EXCHANGE(object, expected, desired, success_order,  \
                                    failure_order)                             \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), compare_exchange)                     \
  ((object), (expected), (desired), (success_order), (failure_order))
#define HAL_ATOMIC_FETCH_ADD(object, value, order)                             \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), fetch_update)                         \
  ((object), (value), (order), false, false)
#define HAL_ATOMIC_FETCH_SUB(object, value, order)                             \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), fetch_update)                         \
  ((object), (value), (order), true, false)
#define HAL_ATOMIC_FETCH_OR(object, value, order)                              \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), fetch_update)                         \
  ((object), (value), (order), false, true)
#define HAL_ATOMIC_ADD_FETCH(object, value, order)                             \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), update_fetch)                         \
  ((object), (value), (order), false)
#define HAL_ATOMIC_SUB_FETCH(object, value, order)                             \
  JH_ATOMIC_MSVC_SCALAR_SELECT((object), update_fetch)                         \
  ((object), (value), (order), true)

#endif

#define HAL_ATOMIC_TEST_AND_SET(object, order)                                 \
  jh_atomic_msvc_test_and_set((object), sizeof(*(object)), (order))
#define HAL_ATOMIC_CLEAR(object, order)                                        \
  jh_atomic_msvc_clear((object), sizeof(*(object)), (order))
#define HAL_ATOMIC_THREAD_FENCE(order) jh_atomic_msvc_thread_fence((order))

#ifdef __cplusplus

#define HAL_ATOMIC_POINTER_LOAD(object, order)                                 \
  jh_atomic_msvc_load((object), (order))
#define HAL_ATOMIC_POINTER_COMPARE_EXCHANGE(object, expected, desired,         \
                                            success_order, failure_order)      \
  jh_atomic_msvc_compare_exchange((object), (expected), (desired),             \
                                  (success_order), (failure_order))

#else

static HAL_FORCE_INLINE void *
jh_atomic_msvc_pointer_load(void *const volatile *object,
                            hal_atomic_memory_order_t order) {
  if (!jh_atomic_msvc_load_order_valid(order)) {
    HAL_TRAP();
  }
  return _InterlockedCompareExchangePointer((void *volatile *)object, NULL,
                                            NULL);
}

static HAL_FORCE_INLINE bool jh_atomic_msvc_pointer_compare_exchange(
    void *volatile *object, void *expected_storage, void *desired,
    hal_atomic_memory_order_t success_order,
    hal_atomic_memory_order_t failure_order) {
  if (!jh_atomic_msvc_compare_orders_valid(success_order, failure_order)) {
    HAL_TRAP();
  }
  void *expected = NULL;
  memcpy(&expected, expected_storage, sizeof(expected));
  void *observed =
      _InterlockedCompareExchangePointer(object, desired, expected);
  if (observed == expected) {
    return true;
  }
  memcpy(expected_storage, &observed, sizeof(observed));
  return false;
}

#define HAL_ATOMIC_POINTER_LOAD(object, order)                                 \
  jh_atomic_msvc_pointer_load((void *const volatile *)(object), (order))
#define HAL_ATOMIC_POINTER_COMPARE_EXCHANGE(object, expected, desired,         \
                                            success_order, failure_order)      \
  jh_atomic_msvc_pointer_compare_exchange(                                     \
      (void *volatile *)(object), (void *)(expected), (void *)(desired),       \
      (success_order), (failure_order))

#endif

#else

#define JH_ATOMIC_UNSUPPORTED() ((void)sizeof(char[-1]))
#define HAL_ATOMIC_LOAD(object, order) (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_STORE(object, value, order) JH_ATOMIC_UNSUPPORTED()
#define HAL_ATOMIC_EXCHANGE(object, value, order)                              \
  (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_COMPARE_EXCHANGE(object, expected, desired, success_order,  \
                                    failure_order)                             \
  (JH_ATOMIC_UNSUPPORTED(), false)
#define HAL_ATOMIC_FETCH_ADD(object, value, order)                             \
  (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_FETCH_SUB(object, value, order)                             \
  (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_FETCH_OR(object, value, order)                              \
  (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_ADD_FETCH(object, value, order)                             \
  (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_SUB_FETCH(object, value, order)                             \
  (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_TEST_AND_SET(object, order) (JH_ATOMIC_UNSUPPORTED(), false)
#define HAL_ATOMIC_CLEAR(object, order) JH_ATOMIC_UNSUPPORTED()
#define HAL_ATOMIC_THREAD_FENCE(order) JH_ATOMIC_UNSUPPORTED()
#define HAL_ATOMIC_POINTER_LOAD(object, order)                                 \
  (JH_ATOMIC_UNSUPPORTED(), *(object))
#define HAL_ATOMIC_POINTER_COMPARE_EXCHANGE(object, expected, desired,         \
                                            success_order, failure_order)      \
  (JH_ATOMIC_UNSUPPORTED(), false)

#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Count leading zero bits of a 32-bit value.
 * @param value Non-zero input; zero is undefined for the GNU builtin and is
 *              reported as 32 here only on compilers without one.
 * @return Number of leading zero bits, 0..31 for a non-zero @p value.
 */
static inline uint32_t hal_clz32(uint32_t value) {
#if HAL_COMPILER_IS_GNU_LIKE
  return (uint32_t)__builtin_clz(value);
#elif HAL_COMPILER_IS_MSVC
  unsigned long index = 0ul;
  return _BitScanReverse(&index, (unsigned long)value) ? (31u - (uint32_t)index)
                                                       : 32u;
#else
  uint32_t count = 0u;
  uint32_t probe = 0x80000000u;
  while (probe != 0u && (value & probe) == 0u) {
    ++count;
    probe >>= 1;
  }
  return count;
#endif
}

#ifdef __cplusplus
}
#endif
