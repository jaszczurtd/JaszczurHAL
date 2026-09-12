#pragma once

#include "hal/core/hal_compiler.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/hal_status.h"
#include "hal/system/hal_sync.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef HAL_PN532_MAX_TRANSPORTS
static constexpr size_t JH_PN532_FRAME_CACHE_SLOTS = HAL_PN532_MAX_TRANSPORTS;
#else
static constexpr size_t JH_PN532_FRAME_CACHE_SLOTS = 4u;
#endif

static constexpr size_t JH_PN532_FRAME_HEADER_SIZE = 5u;
static constexpr size_t JH_PN532_FRAME_OVERHEAD = 7u;
static constexpr size_t JH_PN532_MAX_NORMAL_FRAME_SIZE = 262u;

static_assert(JH_PN532_FRAME_CACHE_SLOTS > 0u,
              "PN532 frame cache requires at least one slot");

class JH_PN532_FRAME_READ_CACHE {
public:
  hal_status_t activate(const void *device, const uint8_t *frame,
                        size_t cached_size, bool tail_discarded) {
    if (device == nullptr || frame == nullptr ||
        cached_size < JH_PN532_FRAME_HEADER_SIZE ||
        cached_size > JH_PN532_MAX_NORMAL_FRAME_SIZE) {
      return HAL_EINVAL;
    }
    hal_mutex_t mutex = jh_hal_mutex_try_create_once(&_mutex);
    if (mutex == nullptr) {
      return HAL_ENOMEM;
    }
    hal_mutex_lock(mutex);
    Slot *slot = find(device);
    if (slot == nullptr) {
      for (Slot &candidate : _slots) {
        if (candidate.device == nullptr) {
          slot = &candidate;
          break;
        }
      }
    }
    if (slot != nullptr) {
      slot->device = device;
      slot->offset = JH_PN532_FRAME_HEADER_SIZE;
      slot->frame_size = (size_t)frame[3] + JH_PN532_FRAME_OVERHEAD;
      slot->cached_size = cached_size;
      slot->tail_discarded = tail_discarded;
      memcpy(slot->data, frame, cached_size);
    }
    hal_mutex_unlock(mutex);
    return slot != nullptr ? HAL_OK : HAL_ENOMEM;
  }

  hal_status_t read(const void *device, uint8_t *data, size_t size,
                    bool *out_cached) {
    if (device == nullptr || (size > 0u && data == nullptr) ||
        out_cached == nullptr) {
      return HAL_EINVAL;
    }
    *out_cached = false;
    hal_mutex_t mutex = HAL_ATOMIC_LOAD(&_mutex, HAL_ATOMIC_ACQUIRE);
    if (mutex == nullptr) {
      return HAL_OK;
    }

    hal_mutex_lock(mutex);
    Slot *slot = find(device);
    if (slot != nullptr) {
      *out_cached = true;
      const size_t available = slot->offset < slot->cached_size
                                   ? slot->cached_size - slot->offset
                                   : 0u;
      const size_t copy_size = size < available ? size : available;
      if (copy_size > 0u) {
        memcpy(data, slot->data + slot->offset, copy_size);
      }
      if (copy_size < size) {
        if (!slot->tail_discarded) {
          *slot = {};
          hal_mutex_unlock(mutex);
          return HAL_EOVERFLOW;
        }
        memset(data + copy_size, 0, size - copy_size);
      }
      slot->offset += size;
      if (slot->offset >= slot->frame_size) {
        slot->device = nullptr;
        slot->offset = 0u;
        slot->frame_size = 0u;
        slot->cached_size = 0u;
        slot->tail_discarded = false;
      }
    }
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }

  hal_status_t release(const void *device) {
    if (device == nullptr) {
      return HAL_EINVAL;
    }
    hal_mutex_t mutex = HAL_ATOMIC_LOAD(&_mutex, HAL_ATOMIC_ACQUIRE);
    if (mutex == nullptr) {
      return HAL_OK;
    }

    hal_mutex_lock(mutex);
    Slot *slot = find(device);
    if (slot != nullptr) {
      *slot = {};
    }
    hal_mutex_unlock(mutex);
    return HAL_OK;
  }

private:
  struct Slot {
    const void *device = nullptr;
    size_t offset = 0u;
    size_t frame_size = 0u;
    size_t cached_size = 0u;
    bool tail_discarded = false;
    uint8_t data[JH_PN532_MAX_NORMAL_FRAME_SIZE] = {};
  };

  Slot *find(const void *device) {
    for (Slot &slot : _slots) {
      if (slot.device == device) {
        return &slot;
      }
    }
    return nullptr;
  }

  Slot _slots[JH_PN532_FRAME_CACHE_SLOTS] = {};
  hal_mutex_t _mutex = nullptr;
};
