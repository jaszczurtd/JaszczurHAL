#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t generation;
  bool active;
  bool completed;
  bool found;
  uint8_t address[4];
} jh_dns_ipv4_request_state_t;

static inline uint32_t
jh_dns_ipv4_request_begin(jh_dns_ipv4_request_state_t *state) {
  if (state == NULL || state->active) {
    return 0u;
  }

  ++state->generation;
  if (state->generation == 0u) {
    state->generation = 1u;
  }
  state->active = true;
  state->completed = false;
  state->found = false;
  memset(state->address, 0, sizeof(state->address));
  return state->generation;
}

static inline bool
jh_dns_ipv4_request_complete(jh_dns_ipv4_request_state_t *state,
                             uint32_t generation, bool found,
                             const uint8_t address[4]) {
  if (state == NULL || generation == 0u || !state->active ||
      state->generation != generation) {
    return false;
  }

  state->active = false;
  state->completed = true;
  state->found = found && address != NULL;
  if (state->found) {
    memcpy(state->address, address, sizeof(state->address));
  } else {
    memset(state->address, 0, sizeof(state->address));
  }
  return true;
}

static inline bool
jh_dns_ipv4_request_cancel(jh_dns_ipv4_request_state_t *state,
                           uint32_t generation) {
  if (state == NULL || generation == 0u || !state->active ||
      state->generation != generation) {
    return false;
  }

  state->active = false;
  state->completed = false;
  state->found = false;
  memset(state->address, 0, sizeof(state->address));
  return true;
}

#ifdef __cplusplus
}
#endif
