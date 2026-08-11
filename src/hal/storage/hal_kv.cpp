#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_KV

#include "hal/storage/hal_kv.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/serial/hal_serial.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/system/hal_sync.h"

#include <string.h>

namespace {

constexpr uint16_t KV_BANK_MAGIC = 0x4B56; // 'KV'
constexpr uint8_t KV_BANK_VERSION = 1;
constexpr uint16_t KV_REC_MAGIC = 0xA55A;
constexpr uint8_t KV_REC_TYPE_U32 = 1;
constexpr uint8_t KV_REC_TYPE_BLOB = 2;
constexpr uint8_t KV_REC_TYPE_DELETE = 3;
constexpr uint8_t KV_REC_VALID = 0xA5;

constexpr uint16_t KV_BANK_HDR_SIZE = 16;
constexpr uint16_t KV_REC_HDR_SIZE = 14;
constexpr uint16_t KV_REC_FTR_SIZE = 3;
constexpr uint16_t KV_REC_OVERHEAD = KV_REC_HDR_SIZE + KV_REC_FTR_SIZE;
constexpr uint16_t KV_MAX_KEYS = 32;

struct kv_bank_hdr_t {
  uint16_t magic;
  uint8_t version;
  uint8_t reserved;
  uint32_t generation;
  uint16_t used_offset;
  uint16_t reserved2;
  uint16_t reserved3;
  uint16_t crc;
};

struct kv_rec_hdr_t {
  uint16_t magic;
  uint16_t key;
  uint8_t type;
  uint8_t flags;
  uint16_t len;
  uint32_t seq;
  uint16_t crc;
};

struct kv_index_entry_t {
  bool in_use;
  uint16_t key;
  uint8_t type;
  uint16_t len;
  uint16_t payload_addr;
  uint32_t seq;
};

static hal_mutex_t s_kv_mutex = NULL;
static bool s_ready = false;
static uint16_t s_base = 0;
static uint16_t s_size = 0;
static uint16_t s_bank_size = 0;
static uint16_t s_active_bank = 0; // 0 or 1
static kv_bank_hdr_t s_active_hdr = {};
static uint32_t s_next_seq = 1;
static kv_index_entry_t s_index[KV_MAX_KEYS] = {};

// Architectural flash bug fix: allow callers to coalesce multiple KV writes
// into a single flash commit. Default behavior (s_auto_commit == true) keeps
// the historical per-call commit semantics. When s_auto_commit is false,
// writes are deferred until hal_kv_commit() is called.
static bool s_auto_commit = true;
static bool s_dirty = false;

static hal_status_t kv_mark_dirty_and_maybe_commit(void) {
  if (s_auto_commit) {
    const hal_status_t status = hal_eeprom_commit();
    s_dirty = hal_status_is_error(status);
    return status;
  }
  s_dirty = true;
  return HAL_OK;
}

static inline uint16_t kv_bank_base(uint16_t bank) {
  return (uint16_t)(s_base + bank * s_bank_size);
}

static inline uint16_t kv_payload_capacity(void) {
  return (uint16_t)(s_bank_size - KV_BANK_HDR_SIZE);
}

static void kv_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_kv_mutex);
}

static uint16_t crc16_ccitt_update(uint16_t crc, uint8_t data) {
  crc ^= (uint16_t)data << 8;
  for (uint8_t i = 0; i < 8; i++) {
    if (crc & 0x8000) {
      crc = (uint16_t)((crc << 1) ^ 0x1021);
    } else {
      crc <<= 1;
    }
  }
  return crc;
}

static uint16_t crc16_buf(const uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc = crc16_ccitt_update(crc, buf[i]);
  }
  return crc;
}

static void bank_hdr_to_raw(const kv_bank_hdr_t &h,
                            uint8_t raw[KV_BANK_HDR_SIZE]) {
  raw[0] = (uint8_t)(h.magic & 0xFF);
  raw[1] = (uint8_t)((h.magic >> 8) & 0xFF);
  raw[2] = h.version;
  raw[3] = h.reserved;
  raw[4] = (uint8_t)(h.generation & 0xFF);
  raw[5] = (uint8_t)((h.generation >> 8) & 0xFF);
  raw[6] = (uint8_t)((h.generation >> 16) & 0xFF);
  raw[7] = (uint8_t)((h.generation >> 24) & 0xFF);
  raw[8] = (uint8_t)(h.used_offset & 0xFF);
  raw[9] = (uint8_t)((h.used_offset >> 8) & 0xFF);
  raw[10] = (uint8_t)(h.reserved2 & 0xFF);
  raw[11] = (uint8_t)((h.reserved2 >> 8) & 0xFF);
  raw[12] = (uint8_t)(h.reserved3 & 0xFF);
  raw[13] = (uint8_t)((h.reserved3 >> 8) & 0xFF);
  raw[14] = (uint8_t)(h.crc & 0xFF);
  raw[15] = (uint8_t)((h.crc >> 8) & 0xFF);
}

static hal_status_t bank_hdr_from_eeprom(uint16_t addr, kv_bank_hdr_t *out) {
  if (!out) {
    return HAL_EINVAL;
  }
  *out = {};
  uint8_t raw[KV_BANK_HDR_SIZE];
  const hal_status_t status =
      hal_eeprom_read_bytes(addr, raw, KV_BANK_HDR_SIZE);
  if (hal_status_is_error(status)) {
    return status;
  }
  out->magic = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
  out->version = raw[2];
  out->reserved = raw[3];
  out->generation = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8) |
                    ((uint32_t)raw[6] << 16) | ((uint32_t)raw[7] << 24);
  out->used_offset = (uint16_t)raw[8] | ((uint16_t)raw[9] << 8);
  out->reserved2 = (uint16_t)raw[10] | ((uint16_t)raw[11] << 8);
  out->reserved3 = (uint16_t)raw[12] | ((uint16_t)raw[13] << 8);
  out->crc = (uint16_t)raw[14] | ((uint16_t)raw[15] << 8);
  return HAL_OK;
}

static uint16_t bank_hdr_crc(const kv_bank_hdr_t &h) {
  uint8_t raw[KV_BANK_HDR_SIZE];
  kv_bank_hdr_t tmp = h;
  tmp.crc = 0;
  bank_hdr_to_raw(tmp, raw);
  return crc16_buf(raw, KV_BANK_HDR_SIZE - 2);
}

static bool bank_hdr_valid(const kv_bank_hdr_t &h) {
  if (h.magic != KV_BANK_MAGIC || h.version != KV_BANK_VERSION)
    return false;
  if (h.used_offset < KV_BANK_HDR_SIZE || h.used_offset > s_bank_size)
    return false;
  return bank_hdr_crc(h) == h.crc;
}

static hal_status_t bank_hdr_write(uint16_t bank, const kv_bank_hdr_t &h) {
  const uint16_t base = kv_bank_base(bank);
  uint8_t raw[KV_BANK_HDR_SIZE];
  kv_bank_hdr_t tmp = h;
  tmp.crc = bank_hdr_crc(tmp);
  bank_hdr_to_raw(tmp, raw);
  return hal_eeprom_write_bytes(base, raw, KV_BANK_HDR_SIZE);
}

static kv_bank_hdr_t bank_hdr_make(uint32_t generation) {
  kv_bank_hdr_t h = {};
  h.magic = KV_BANK_MAGIC;
  h.version = KV_BANK_VERSION;
  h.generation = generation;
  h.used_offset = KV_BANK_HDR_SIZE;
  h.crc = bank_hdr_crc(h);
  return h;
}

static hal_status_t rec_hdr_read(uint16_t addr, kv_rec_hdr_t *out) {
  if (!out) {
    return HAL_EINVAL;
  }
  *out = {};
  uint8_t raw[KV_REC_HDR_SIZE];
  const hal_status_t status = hal_eeprom_read_bytes(addr, raw, KV_REC_HDR_SIZE);
  if (hal_status_is_error(status)) {
    return status;
  }
  out->magic = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
  out->key = (uint16_t)raw[2] | ((uint16_t)raw[3] << 8);
  out->type = raw[4];
  out->flags = raw[5];
  out->len = (uint16_t)raw[6] | ((uint16_t)raw[7] << 8);
  out->seq = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8) |
             ((uint32_t)raw[10] << 16) | ((uint32_t)raw[11] << 24);
  out->crc = (uint16_t)raw[12] | ((uint16_t)raw[13] << 8);
  return HAL_OK;
}

static void rec_hdr_encode_prefix(uint8_t *raw, const kv_rec_hdr_t &h) {
  raw[0] = (uint8_t)(h.magic & 0xFFu);
  raw[1] = (uint8_t)((h.magic >> 8u) & 0xFFu);
  raw[2] = (uint8_t)(h.key & 0xFFu);
  raw[3] = (uint8_t)((h.key >> 8u) & 0xFFu);
  raw[4] = h.type;
  raw[5] = h.flags;
  raw[6] = (uint8_t)(h.len & 0xFFu);
  raw[7] = (uint8_t)((h.len >> 8u) & 0xFFu);
  raw[8] = (uint8_t)(h.seq & 0xFFu);
  raw[9] = (uint8_t)((h.seq >> 8u) & 0xFFu);
  raw[10] = (uint8_t)((h.seq >> 16u) & 0xFFu);
  raw[11] = (uint8_t)((h.seq >> 24u) & 0xFFu);
}

static hal_status_t rec_hdr_write(uint16_t addr, const kv_rec_hdr_t &h) {
  uint8_t raw[KV_REC_HDR_SIZE];
  rec_hdr_encode_prefix(raw, h);
  raw[12] = (uint8_t)(h.crc & 0xFF);
  raw[13] = (uint8_t)((h.crc >> 8) & 0xFF);
  return hal_eeprom_write_bytes(addr, raw, KV_REC_HDR_SIZE);
}

static uint16_t rec_hdr_crc(const kv_rec_hdr_t &h) {
  uint8_t raw[12];
  rec_hdr_encode_prefix(raw, h);
  return crc16_buf(raw, sizeof(raw));
}

static int index_find(uint16_t key) {
  for (uint16_t i = 0; i < KV_MAX_KEYS; i++) {
    if (s_index[i].in_use && s_index[i].key == key)
      return (int)i;
  }
  return -1;
}

static int index_alloc(void) {
  for (uint16_t i = 0; i < KV_MAX_KEYS; i++) {
    if (!s_index[i].in_use)
      return (int)i;
  }
  return -1;
}

static uint16_t index_count(void) {
  uint16_t c = 0;
  for (uint16_t i = 0; i < KV_MAX_KEYS; i++) {
    if (s_index[i].in_use)
      c++;
  }
  return c;
}

static hal_status_t value_equals_at(uint16_t payload_addr, const uint8_t *data,
                                    uint16_t len, bool *out_equal) {
  if (!out_equal) {
    return HAL_EINVAL;
  }
  *out_equal = false;
  uint8_t chunk[32];
  uint16_t off = 0;
  while (off < len) {
    uint16_t n = (uint16_t)(len - off);
    if (n > sizeof(chunk))
      n = sizeof(chunk);
    const hal_status_t status =
        hal_eeprom_read_bytes((uint16_t)(payload_addr + off), chunk, n);
    if (hal_status_is_error(status)) {
      return status;
    }
    if (memcmp(chunk, data + off, n) != 0) {
      return HAL_OK;
    }
    off = (uint16_t)(off + n);
  }
  *out_equal = true;
  return HAL_OK;
}

static hal_status_t append_record_raw(uint16_t bank, kv_bank_hdr_t &hdr,
                                      uint16_t key, uint8_t type,
                                      const uint8_t *data, uint16_t len,
                                      uint32_t seq) {
  const uint16_t total = (uint16_t)(KV_REC_OVERHEAD + len);
  if ((uint32_t)hdr.used_offset + total > s_bank_size) {
    return HAL_ENOMEM;
  }

  const uint16_t rec_addr = (uint16_t)(kv_bank_base(bank) + hdr.used_offset);
  const uint16_t payload_addr = (uint16_t)(rec_addr + KV_REC_HDR_SIZE);
  const uint16_t ftr_addr = (uint16_t)(payload_addr + len);

  kv_rec_hdr_t rh = {};
  rh.magic = KV_REC_MAGIC;
  rh.key = key;
  rh.type = type;
  rh.flags = 0;
  rh.len = len;
  rh.seq = seq;
  rh.crc = rec_hdr_crc(rh);

  hal_status_t status = rec_hdr_write(rec_addr, rh);
  if (hal_status_is_error(status)) {
    return status;
  }

  // Payload write: single batched EEPROM transaction; CRC computed in RAM.
  uint16_t vcrc = 0xFFFF;
  if (len > 0) {
    if (data) {
      status = hal_eeprom_write_bytes(payload_addr, data, len);
      if (hal_status_is_error(status)) {
        return status;
      }
      vcrc = crc16_buf(data, len);
    } else {
      uint8_t zeros[32] = {};
      uint16_t off = 0;
      while (off < len) {
        uint16_t n = (uint16_t)(len - off);
        if (n > sizeof(zeros))
          n = sizeof(zeros);
        status =
            hal_eeprom_write_bytes((uint16_t)(payload_addr + off), zeros, n);
        if (hal_status_is_error(status)) {
          return status;
        }
        for (uint16_t i = 0; i < n; i++)
          vcrc = crc16_ccitt_update(vcrc, 0);
        off = (uint16_t)(off + n);
      }
    }
  }

  uint8_t ftr[KV_REC_FTR_SIZE];
  ftr[0] = (uint8_t)(vcrc & 0xFF);
  ftr[1] = (uint8_t)((vcrc >> 8) & 0xFF);
  ftr[2] = KV_REC_VALID;
  status = hal_eeprom_write_bytes(ftr_addr, ftr, KV_REC_FTR_SIZE);
  if (hal_status_is_error(status)) {
    return status;
  }

  hdr.used_offset = (uint16_t)(hdr.used_offset + total);
  status = bank_hdr_write(bank, hdr);
  if (hal_status_is_error(status)) {
    return status;
  }
  return kv_mark_dirty_and_maybe_commit();
}

static hal_status_t append_record_current(uint16_t key, uint8_t type,
                                          const uint8_t *data, uint16_t len,
                                          uint32_t seq) {
  return append_record_raw(s_active_bank, s_active_hdr, key, type, data, len,
                           seq);
}

static hal_status_t scan_active_and_build_index(void) {
  memset(s_index, 0, sizeof(s_index));

  uint32_t max_seq = 0;
  uint16_t bank_base = kv_bank_base(s_active_bank);
  uint16_t off = KV_BANK_HDR_SIZE;

  while ((uint32_t)off + KV_REC_OVERHEAD <= s_active_hdr.used_offset) {
    const uint16_t rec_addr = (uint16_t)(bank_base + off);
    kv_rec_hdr_t rh = {};
    hal_status_t status = rec_hdr_read(rec_addr, &rh);
    if (hal_status_is_error(status)) {
      return status;
    }
    if (rh.magic != KV_REC_MAGIC)
      break;
    if (rh.crc != rec_hdr_crc(rh))
      break;

    const uint16_t total = (uint16_t)(KV_REC_OVERHEAD + rh.len);
    if ((uint32_t)off + total > s_active_hdr.used_offset)
      break;

    const uint16_t payload_addr = (uint16_t)(rec_addr + KV_REC_HDR_SIZE);
    const uint16_t ftr_addr = (uint16_t)(payload_addr + rh.len);
    uint8_t ftr[KV_REC_FTR_SIZE];
    status = hal_eeprom_read_bytes(ftr_addr, ftr, KV_REC_FTR_SIZE);
    if (hal_status_is_error(status)) {
      return status;
    }
    const uint16_t stored_vcrc = (uint16_t)ftr[0] | ((uint16_t)ftr[1] << 8);
    const uint8_t valid = ftr[2];
    if (valid != KV_REC_VALID)
      break;

    uint16_t calc_vcrc = 0xFFFF;
    {
      uint8_t chunk[32];
      uint16_t off = 0;
      while (off < rh.len) {
        uint16_t n = (uint16_t)(rh.len - off);
        if (n > sizeof(chunk))
          n = sizeof(chunk);
        status =
            hal_eeprom_read_bytes((uint16_t)(payload_addr + off), chunk, n);
        if (hal_status_is_error(status)) {
          return status;
        }
        for (uint16_t i = 0; i < n; i++) {
          calc_vcrc = crc16_ccitt_update(calc_vcrc, chunk[i]);
        }
        off = (uint16_t)(off + n);
      }
    }
    if (calc_vcrc != stored_vcrc)
      break;

    if (rh.seq > max_seq)
      max_seq = rh.seq;

    int idx = index_find(rh.key);
    if (rh.type == KV_REC_TYPE_DELETE) {
      if (idx >= 0) {
        s_index[idx].in_use = false;
      }
    } else {
      if (idx < 0)
        idx = index_alloc();
      if (idx < 0) {
        hal_derr("hal_kv: key index full, increase KV_MAX_KEYS");
        return HAL_ENOMEM;
      }
      s_index[idx].in_use = true;
      s_index[idx].key = rh.key;
      s_index[idx].type = rh.type;
      s_index[idx].len = rh.len;
      s_index[idx].payload_addr = payload_addr;
      s_index[idx].seq = rh.seq;
    }

    off = (uint16_t)(off + total);
  }

  s_next_seq = max_seq + 1;
  return HAL_OK;
}

static hal_status_t gc_locked(void) {
  const uint16_t dst_bank = (s_active_bank == 0) ? 1 : 0;
  kv_bank_hdr_t dst_hdr = bank_hdr_make(s_active_hdr.generation + 1);

  // Copy only live records, preserving key/value and assigning new sequence.
  for (uint16_t i = 0; i < KV_MAX_KEYS; i++) {
    if (!s_index[i].in_use)
      continue;

    const uint16_t len = s_index[i].len;
    const uint16_t src_payload = s_index[i].payload_addr;

    // Stream payload through a bounded stack buffer to avoid large RAM use.
    uint8_t chunk[32];
    const uint16_t rec_total = (uint16_t)(KV_REC_OVERHEAD + len);
    if ((uint32_t)dst_hdr.used_offset + rec_total > s_bank_size) {
      hal_derr("hal_kv_gc: destination bank too small");
      return HAL_ENOMEM;
    }

    const uint16_t rec_addr =
        (uint16_t)(kv_bank_base(dst_bank) + dst_hdr.used_offset);
    const uint16_t payload_addr = (uint16_t)(rec_addr + KV_REC_HDR_SIZE);
    const uint16_t ftr_addr = (uint16_t)(payload_addr + len);

    kv_rec_hdr_t rh = {};
    rh.magic = KV_REC_MAGIC;
    rh.key = s_index[i].key;
    rh.type = s_index[i].type;
    rh.flags = 0;
    rh.len = len;
    rh.seq = s_next_seq++;
    rh.crc = rec_hdr_crc(rh);
    hal_status_t status = rec_hdr_write(rec_addr, rh);
    if (hal_status_is_error(status)) {
      return status;
    }

    uint16_t vcrc = 0xFFFF;
    uint16_t copied = 0;
    while (copied < len) {
      uint16_t n = (uint16_t)(len - copied);
      if (n > sizeof(chunk))
        n = sizeof(chunk);
      status =
          hal_eeprom_read_bytes((uint16_t)(src_payload + copied), chunk, n);
      if (hal_status_is_error(status)) {
        return status;
      }
      status =
          hal_eeprom_write_bytes((uint16_t)(payload_addr + copied), chunk, n);
      if (hal_status_is_error(status)) {
        return status;
      }
      for (uint16_t c = 0; c < n; c++) {
        vcrc = crc16_ccitt_update(vcrc, chunk[c]);
      }
      copied = (uint16_t)(copied + n);
    }

    uint8_t ftr[KV_REC_FTR_SIZE];
    ftr[0] = (uint8_t)(vcrc & 0xFF);
    ftr[1] = (uint8_t)((vcrc >> 8) & 0xFF);
    ftr[2] = KV_REC_VALID;
    status = hal_eeprom_write_bytes(ftr_addr, ftr, KV_REC_FTR_SIZE);
    if (hal_status_is_error(status)) {
      return status;
    }

    dst_hdr.used_offset = (uint16_t)(dst_hdr.used_offset + rec_total);
  }

  // BUG-12 fix: write destination header only AFTER all records are copied.
  // This way a power-loss during copy leaves the destination without a valid
  // header, and recovery will use the old (complete) source bank.
  hal_status_t status = bank_hdr_write(dst_bank, dst_hdr);
  if (hal_status_is_error(status)) {
    return status;
  }
  status = kv_mark_dirty_and_maybe_commit();
  if (hal_status_is_error(status)) {
    return status;
  }

  s_active_bank = dst_bank;
  s_active_hdr = dst_hdr;
  return scan_active_and_build_index();
}

static hal_status_t ensure_space_locked(uint16_t needed_total) {
  if ((uint32_t)s_active_hdr.used_offset + needed_total <= s_bank_size) {
    return HAL_OK;
  }
  const hal_status_t status = gc_locked();
  if (hal_status_is_error(status)) {
    return status;
  }
  return (uint32_t)s_active_hdr.used_offset + needed_total <= s_bank_size
             ? HAL_OK
             : HAL_ENOMEM;
}

static hal_status_t set_blob_locked(uint16_t key, uint8_t type,
                                    const uint8_t *data, uint16_t len) {
  if (len > kv_payload_capacity()) {
    hal_derr("hal_kv_set_blob: value too large (%u)", (unsigned)len);
    return HAL_EOVERFLOW;
  }

  int idx = index_find(key);
  if (idx >= 0 && s_index[idx].type == type && s_index[idx].len == len) {
    bool equal = false;
    const hal_status_t compare_status =
        value_equals_at(s_index[idx].payload_addr, data, len, &equal);
    if (hal_status_is_error(compare_status)) {
      return compare_status;
    }
    if (equal) {
      // Slow EEPROM optimization: avoid writing unchanged value.
      return HAL_OK;
    }
  }

  // BUG-11 fix: check index space BEFORE writing to EEPROM.
  if (idx < 0 && index_alloc() < 0) {
    hal_derr("hal_kv: key index full, increase KV_MAX_KEYS");
    return HAL_ENOMEM;
  }

  const uint16_t needed = (uint16_t)(KV_REC_OVERHEAD + len);
  const hal_status_t space_status = ensure_space_locked(needed);
  if (hal_status_is_error(space_status)) {
    hal_derr("hal_kv_set_blob: not enough space for key=%u", (unsigned)key);
    return space_status;
  }

  // Compute payload address now - used_offset is stable after
  // ensure_space_locked.
  const uint16_t payload_addr =
      (uint16_t)(kv_bank_base(s_active_bank) + s_active_hdr.used_offset +
                 KV_REC_HDR_SIZE);
  // BUG-15 fix: save seq and restore on failure.
  const uint32_t saved_seq = s_next_seq;
  const uint32_t seq = s_next_seq++;
  const hal_status_t append_status =
      append_record_current(key, type, data, len, seq);
  if (hal_status_is_error(append_status)) {
    s_next_seq = saved_seq;
    return append_status;
  }

  // Update index directly - no EEPROM rescan needed.
  idx = index_find(key);
  if (idx < 0)
    idx = index_alloc();
  s_index[idx].in_use = true;
  s_index[idx].key = key;
  s_index[idx].type = type;
  s_index[idx].len = len;
  s_index[idx].payload_addr = payload_addr;
  s_index[idx].seq = seq;
  return HAL_OK;
}

static hal_status_t delete_locked(uint16_t key) {
  if (index_find(key) < 0)
    return HAL_OK;

  const hal_status_t space_status = ensure_space_locked(KV_REC_OVERHEAD);
  if (hal_status_is_error(space_status)) {
    hal_derr("hal_kv_delete: not enough space for tombstone");
    return space_status;
  }

  // BUG-15 fix: save seq and restore on failure.
  const uint32_t saved_seq = s_next_seq;
  const uint32_t seq = s_next_seq++;
  const hal_status_t append_status =
      append_record_current(key, KV_REC_TYPE_DELETE, nullptr, 0, seq);
  if (hal_status_is_error(append_status)) {
    s_next_seq = saved_seq;
    return append_status;
  }

  // Update index directly - no EEPROM rescan needed.
  const int idx = index_find(key);
  if (idx >= 0)
    s_index[idx].in_use = false;
  return HAL_OK;
}

} // namespace

hal_status_t hal_kv_init_ex(uint16_t base_addr, uint16_t size_bytes) {
  kv_ensure_mutex();
  if (!s_kv_mutex) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);

  s_ready = false;
  s_base = base_addr;
  s_size = size_bytes;
  s_bank_size = (uint16_t)(size_bytes / 2U);

  if (s_bank_size < (KV_BANK_HDR_SIZE + KV_REC_OVERHEAD)) {
    hal_derr("hal_kv_init: storage area too small (%u)", (unsigned)size_bytes);
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EINVAL;
  }

  uint16_t eeprom_size = 0u;
  hal_status_t status = hal_eeprom_size_ex(&eeprom_size);
  if (hal_status_is_error(status)) {
    hal_mutex_unlock(s_kv_mutex);
    return status;
  }
  if ((uint32_t)base_addr + size_bytes > eeprom_size) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EOVERFLOW;
  }

  kv_bank_hdr_t h0 = {};
  kv_bank_hdr_t h1 = {};
  status = bank_hdr_from_eeprom(kv_bank_base(0), &h0);
  if (hal_status_is_ok(status)) {
    status = bank_hdr_from_eeprom(kv_bank_base(1), &h1);
  }
  if (hal_status_is_error(status)) {
    hal_mutex_unlock(s_kv_mutex);
    return status;
  }
  const bool v0 = bank_hdr_valid(h0);
  const bool v1 = bank_hdr_valid(h1);

  if (!v0 && !v1) {
    h0 = bank_hdr_make(1);
    h1 = bank_hdr_make(0);
    status = bank_hdr_write(0, h0);
    if (hal_status_is_ok(status)) {
      status = bank_hdr_write(1, h1);
    }
    // init path always commits regardless of deferred mode
    if (hal_status_is_ok(status)) {
      status = hal_eeprom_commit();
    }
    s_dirty = hal_status_is_error(status);
    if (hal_status_is_error(status)) {
      hal_mutex_unlock(s_kv_mutex);
      return status;
    }
  }

  status = bank_hdr_from_eeprom(kv_bank_base(0), &h0);
  if (hal_status_is_ok(status)) {
    status = bank_hdr_from_eeprom(kv_bank_base(1), &h1);
  }
  if (hal_status_is_error(status)) {
    hal_mutex_unlock(s_kv_mutex);
    return status;
  }
  const bool hv0 = bank_hdr_valid(h0);
  const bool hv1 = bank_hdr_valid(h1);

  if (!hv0 && !hv1) {
    hal_derr("hal_kv_init: failed to initialize bank headers");
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EIO;
  }

  if (hv0 && (!hv1 || h0.generation >= h1.generation)) {
    s_active_bank = 0;
    s_active_hdr = h0;
  } else {
    s_active_bank = 1;
    s_active_hdr = h1;
  }

  status = scan_active_and_build_index();
  s_ready = hal_status_is_ok(status);
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_init(uint16_t base_addr, uint16_t size_bytes) {
  return hal_status_to_bool(hal_kv_init_ex(base_addr, size_bytes));
}

hal_status_t hal_kv_set_u32_ex(uint16_t key, uint32_t value) {
  uint8_t raw[4];
  raw[0] = (uint8_t)(value & 0xFF);
  raw[1] = (uint8_t)((value >> 8) & 0xFF);
  raw[2] = (uint8_t)((value >> 16) & 0xFF);
  raw[3] = (uint8_t)((value >> 24) & 0xFF);

  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  const hal_status_t status =
      s_ready ? set_blob_locked(key, KV_REC_TYPE_U32, raw, sizeof(raw))
              : HAL_EUNINIT;
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_set_u32(uint16_t key, uint32_t value) {
  return hal_status_to_bool(hal_kv_set_u32_ex(key, value));
}

hal_status_t hal_kv_get_u32_ex(uint16_t key, uint32_t *out_value) {
  if (!out_value) {
    hal_derr("hal_kv_get_u32: out_value is NULL");
    return HAL_EINVAL;
  }
  *out_value = 0u;

  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  if (!s_ready) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EUNINIT;
  }

  const int idx = index_find(key);
  if (idx < 0 || s_index[idx].type != KV_REC_TYPE_U32 ||
      s_index[idx].len != 4) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_ENOENT;
  }

  const uint16_t p = s_index[idx].payload_addr;
  uint8_t raw[4];
  const hal_status_t status = hal_eeprom_read_bytes(p, raw, 4);
  if (hal_status_is_error(status)) {
    hal_mutex_unlock(s_kv_mutex);
    return status;
  }
  *out_value = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
               ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);

  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

bool hal_kv_get_u32(uint16_t key, uint32_t *out_value) {
  return hal_status_to_bool(hal_kv_get_u32_ex(key, out_value));
}

hal_status_t hal_kv_set_blob_ex(uint16_t key, const uint8_t *data,
                                uint16_t len) {
  if (len > 0 && !data) {
    hal_derr("hal_kv_set_blob: data is NULL while len > 0");
    return HAL_EINVAL;
  }

  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  const hal_status_t status =
      s_ready ? set_blob_locked(key, KV_REC_TYPE_BLOB, data, len) : HAL_EUNINIT;
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_set_blob(uint16_t key, const uint8_t *data, uint16_t len) {
  return hal_status_to_bool(hal_kv_set_blob_ex(key, data, len));
}

hal_status_t hal_kv_get_blob_ex(uint16_t key, uint8_t *out, uint16_t out_size,
                                uint16_t *out_len) {
  if (out_len) {
    *out_len = 0u;
  }
  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  if (!s_ready) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EUNINIT;
  }

  const int idx = index_find(key);
  if (idx < 0) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_ENOENT;
  }
  if (s_index[idx].type != KV_REC_TYPE_BLOB) {
    hal_derr("hal_kv_get_blob: key=%u exists but has wrong type (%u)",
             (unsigned)key, (unsigned)s_index[idx].type);
    hal_mutex_unlock(s_kv_mutex);
    return HAL_ENOENT;
  }

  if (out_len)
    *out_len = s_index[idx].len;

  if (!out) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_OK;
  }
  if (out_size < s_index[idx].len) {
    hal_derr("hal_kv_get_blob: buffer too small for key=%u", (unsigned)key);
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EOVERFLOW;
  }

  const hal_status_t status =
      hal_eeprom_read_bytes(s_index[idx].payload_addr, out, s_index[idx].len);

  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_get_blob(uint16_t key, uint8_t *out, uint16_t out_size,
                     uint16_t *out_len) {
  return hal_status_to_bool(hal_kv_get_blob_ex(key, out, out_size, out_len));
}

hal_status_t hal_kv_delete_ex(uint16_t key) {
  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  const hal_status_t status = s_ready ? delete_locked(key) : HAL_EUNINIT;
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_delete(uint16_t key) {
  return hal_status_to_bool(hal_kv_delete_ex(key));
}

hal_status_t hal_kv_gc_ex(void) {
  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  const hal_status_t status = s_ready ? gc_locked() : HAL_EUNINIT;
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_gc(void) { return hal_status_to_bool(hal_kv_gc_ex()); }

hal_status_t hal_kv_get_stats_ex(hal_kv_stats_t *out_stats) {
  if (!out_stats) {
    hal_derr("hal_kv_get_stats: out_stats is NULL");
    return HAL_EINVAL;
  }
  *out_stats = {};

  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  if (!s_ready) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EUNINIT;
  }

  out_stats->generation = s_active_hdr.generation;
  out_stats->used_bytes = s_active_hdr.used_offset;
  out_stats->capacity_bytes = s_bank_size;
  out_stats->key_count = index_count();
  out_stats->next_sequence = s_next_seq;

  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

bool hal_kv_get_stats(hal_kv_stats_t *out_stats) {
  return hal_status_to_bool(hal_kv_get_stats_ex(out_stats));
}

hal_status_t hal_kv_set_auto_commit(bool enabled) {
  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  s_auto_commit = enabled;
  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

hal_status_t hal_kv_commit_ex(void) {
  kv_ensure_mutex();
  if (!s_kv_mutex)
    return HAL_ENOMEM;
  hal_mutex_lock(s_kv_mutex);
  if (!s_ready) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EUNINIT;
  }
  hal_status_t status = HAL_OK;
  if (s_dirty) {
    status = hal_eeprom_commit();
    if (hal_status_is_ok(status)) {
      s_dirty = false;
    }
  }
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_commit(void) { return hal_status_to_bool(hal_kv_commit_ex()); }

#endif /* HAL_ENABLE_KV */
