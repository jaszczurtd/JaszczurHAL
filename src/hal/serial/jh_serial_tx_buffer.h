#pragma once

/* Variable-length serial records. The shared serial core serializes access.
 * Formatting writes an unpublished transaction; overflow discards the whole
 * message. The consumer retains its offset across partial transport writes. */
#include "hal/core/hal_layout_check.h"
#include "hal/core/jh_endian.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { JH_SERIAL_TX_HEADER_SIZE = 4U };

typedef struct {
  uint8_t *storage;
  uint32_t capacity;
  uint32_t read;
  uint32_t write;
  uint32_t used;
  uint32_t offset;
  uint32_t staged;
  uint32_t dropped;
  uint8_t kind;
  bool failed;
  bool started;
} jh_serial_tx_buffer_t;

static inline uint32_t jh_serial_tx_position(const jh_serial_tx_buffer_t *queue,
                                             uint32_t position,
                                             uint32_t count) {
  const uint32_t next = position + count;
  return next >= queue->capacity ? next - queue->capacity : next;
}

static inline void jh_serial_tx_copy_in(jh_serial_tx_buffer_t *queue,
                                        uint32_t position, const void *data,
                                        uint32_t length) {
  if (length == 0U) {
    return;
  }
  const uint32_t room = queue->capacity - position;
  const uint32_t first = length < room ? length : room;
  memcpy(queue->storage + position, data, first);
  if (length > first) {
    memcpy(queue->storage, (const uint8_t *)data + first, length - first);
  }
}

static inline void jh_serial_tx_begin(jh_serial_tx_buffer_t *queue,
                                      uint8_t kind) {
  queue->kind = kind;
  queue->staged = 0U;
  queue->failed = queue->capacity - queue->used < JH_SERIAL_TX_HEADER_SIZE;
}

static inline void jh_serial_tx_append(jh_serial_tx_buffer_t *queue,
                                       const void *data, size_t length) {
  if (queue->failed) {
    return;
  }
  const uint32_t free =
      queue->capacity - queue->used - JH_SERIAL_TX_HEADER_SIZE;
  if (length > free - queue->staged) {
    queue->failed = true;
    return;
  }
  const uint32_t position = jh_serial_tx_position(
      queue, queue->write, JH_SERIAL_TX_HEADER_SIZE + queue->staged);
  jh_serial_tx_copy_in(queue, position, data, (uint32_t)length);
  queue->staged += (uint32_t)length;
}

static inline bool jh_serial_tx_commit(jh_serial_tx_buffer_t *queue) {
  if (queue->failed) {
    ++queue->dropped;
    return false;
  }
  if (queue->staged == 0U) {
    return true;
  }
  uint8_t header[JH_SERIAL_TX_HEADER_SIZE] = {0};
  jh_store_le16(header, (uint16_t)queue->staged);
  header[2] = queue->kind;
  jh_serial_tx_copy_in(queue, queue->write, header, sizeof(header));
  const uint32_t bytes = JH_SERIAL_TX_HEADER_SIZE + queue->staged;
  queue->write = jh_serial_tx_position(queue, queue->write, bytes);
  queue->used += bytes;
  return true;
}

static inline uint16_t jh_serial_tx_length(const jh_serial_tx_buffer_t *queue,
                                           uint8_t *kind) {
  uint8_t header[JH_SERIAL_TX_HEADER_SIZE];
  const uint32_t room = queue->capacity - queue->read;
  const uint32_t first = room < sizeof(header) ? room : sizeof(header);
  memcpy(header, queue->storage + queue->read, first);
  if (first < sizeof(header)) {
    memcpy(header + first, queue->storage, sizeof(header) - first);
  }
  *kind = header[2];
  return jh_load_le16(header);
}

static inline size_t jh_serial_tx_chunk(const jh_serial_tx_buffer_t *queue,
                                        size_t budget, const char **data,
                                        uint8_t *kind) {
  if (queue->used == 0U || budget == 0U) {
    return 0U;
  }
  const uint32_t remaining =
      (uint32_t)jh_serial_tx_length(queue, kind) - queue->offset;
  const uint32_t position = jh_serial_tx_position(
      queue, queue->read, JH_SERIAL_TX_HEADER_SIZE + queue->offset);
  const uint32_t contiguous = queue->capacity - position;
  size_t count = remaining < contiguous ? remaining : contiguous;
  if (count > budget) {
    count = budget;
  }
  *data = (const char *)(queue->storage + position);
  return count;
}

static inline void jh_serial_tx_advance(jh_serial_tx_buffer_t *queue,
                                        size_t count) {
  uint8_t kind;
  const uint16_t length = jh_serial_tx_length(queue, &kind);
  queue->offset += (uint32_t)count;
  if (queue->offset == length) {
    const uint32_t bytes = JH_SERIAL_TX_HEADER_SIZE + (uint32_t)length;
    queue->read = jh_serial_tx_position(queue, queue->read, bytes);
    queue->used -= bytes;
    queue->offset = 0U;
    queue->started = false;
  }
}
