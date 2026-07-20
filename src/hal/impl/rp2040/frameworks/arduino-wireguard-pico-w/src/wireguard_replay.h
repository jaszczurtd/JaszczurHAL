#ifndef WIREGUARD_REPLAY_H
#define WIREGUARD_REPLAY_H

#include <stdbool.h>
#include <stdint.h>

static inline bool wireguard_replay_check(uint32_t *bitmap, uint64_t *counter,
                                          uint64_t sequence) {
  const uint64_t window_bits = (uint64_t)(sizeof(*bitmap) * 8u);

  if (sequence > *counter) {
    const uint64_t difference = sequence - *counter;
    if (difference < window_bits) {
      *bitmap = (*bitmap << (uint32_t)difference) | UINT32_C(1);
    } else {
      *bitmap = UINT32_C(1);
    }
    *counter = sequence;
    return true;
  }

  const uint64_t difference = *counter - sequence;
  if (difference >= window_bits) {
    return false;
  }

  const uint32_t mask = UINT32_C(1) << (uint32_t)difference;
  if ((*bitmap & mask) != 0u) {
    return false;
  }

  *bitmap |= mask;
  return true;
}

#endif
