#pragma once

/**
 * @file jh_spsc_ring.h
 * @brief Lock-free queue of fixed-size elements with one producer and one
 *        consumer, for handing data from an interrupt to a task.
 *
 * The producer only writes @c head and the consumer only writes @c tail. Both
 * run modulo twice the capacity, so a full ring (head - tail == capacity) and
 * an empty one (head == tail) stay apart for any capacity. Element data is
 * published with a
 * release store of @c head and read after an acquire load, which keeps the
 * order on Cortex-M, RP2040 (both cores) and the host. A second producer or a
 * second consumer must be serialized by the caller.
 */

#include "hal/core/hal_compiler.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Ring state; storage is owned by the caller. */
typedef struct {
  uint8_t *storage; /**< capacity * elem_size bytes. */
  uint16_t elem_size;
  uint16_t capacity; /**< Elements; 0 makes every push fail. */
  uint32_t head;     /**< Push position, 0 .. 2 * capacity - 1 (producer). */
  uint32_t tail;     /**< Pop position, 0 .. 2 * capacity - 1 (consumer). */
} jh_spsc_ring_t;

/* Positions run over 0 .. 2 * capacity - 1. */
static inline uint32_t jh_spsc_ring_used(const jh_spsc_ring_t *ring,
                                         uint32_t head, uint32_t tail) {
  return head >= tail ? head - tail : head + 2u * ring->capacity - tail;
}

static inline uint32_t jh_spsc_ring_next(const jh_spsc_ring_t *ring,
                                         uint32_t position) {
  return position + 1u == 2u * ring->capacity ? 0u : position + 1u;
}

static inline uint8_t *jh_spsc_ring_slot(const jh_spsc_ring_t *ring,
                                         uint32_t position) {
  const uint32_t index =
      position < ring->capacity ? position : position - ring->capacity;
  return ring->storage + (size_t)index * ring->elem_size;
}

/** @brief Attach storage and empty the ring. Not concurrent with use. */
static inline void jh_spsc_ring_init(jh_spsc_ring_t *ring, void *storage,
                                     uint16_t elem_size, uint16_t capacity) {
  ring->storage = (uint8_t *)storage;
  ring->elem_size = elem_size;
  ring->capacity = storage != NULL ? capacity : 0u;
  ring->head = 0u;
  ring->tail = 0u;
}

/** @brief Elements waiting; callable from either side. */
static inline uint32_t jh_spsc_ring_count(const jh_spsc_ring_t *ring) {
  return jh_spsc_ring_used(ring,
                           HAL_ATOMIC_LOAD(&ring->head, HAL_ATOMIC_ACQUIRE),
                           HAL_ATOMIC_LOAD(&ring->tail, HAL_ATOMIC_ACQUIRE));
}

/** @brief Producer: copy @p elem in; false when the ring is full. */
static inline bool jh_spsc_ring_push(jh_spsc_ring_t *ring, const void *elem) {
  const uint32_t head = HAL_ATOMIC_LOAD(&ring->head, HAL_ATOMIC_RELAXED);
  const uint32_t tail = HAL_ATOMIC_LOAD(&ring->tail, HAL_ATOMIC_ACQUIRE);
  if (jh_spsc_ring_used(ring, head, tail) >= ring->capacity) {
    return false;
  }
  memcpy(jh_spsc_ring_slot(ring, head), elem, ring->elem_size);
  HAL_ATOMIC_STORE(&ring->head, jh_spsc_ring_next(ring, head),
                   HAL_ATOMIC_RELEASE);
  return true;
}

/** @brief Consumer: the oldest element without removing it, or NULL. */
static inline const void *jh_spsc_ring_peek(const jh_spsc_ring_t *ring) {
  const uint32_t tail = HAL_ATOMIC_LOAD(&ring->tail, HAL_ATOMIC_RELAXED);
  const uint32_t head = HAL_ATOMIC_LOAD(&ring->head, HAL_ATOMIC_ACQUIRE);
  if (head == tail) {
    return NULL;
  }
  return jh_spsc_ring_slot(ring, tail);
}

/** @brief Consumer: drop the element jh_spsc_ring_peek() returned. */
static inline void jh_spsc_ring_drop(jh_spsc_ring_t *ring) {
  const uint32_t tail = HAL_ATOMIC_LOAD(&ring->tail, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&ring->tail, jh_spsc_ring_next(ring, tail),
                   HAL_ATOMIC_RELEASE);
}

/** @brief Consumer: copy the oldest element out; false when empty. */
static inline bool jh_spsc_ring_pop(jh_spsc_ring_t *ring, void *elem) {
  const void *front = jh_spsc_ring_peek(ring);
  if (front == NULL) {
    return false;
  }
  memcpy(elem, front, ring->elem_size);
  jh_spsc_ring_drop(ring);
  return true;
}

/** @brief Consumer: drop everything currently queued. */
static inline void jh_spsc_ring_clear(jh_spsc_ring_t *ring) {
  HAL_ATOMIC_STORE(&ring->tail,
                   HAL_ATOMIC_LOAD(&ring->head, HAL_ATOMIC_ACQUIRE),
                   HAL_ATOMIC_RELEASE);
}

#ifdef __cplusplus
}
#endif
