#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_KV

#include "hal/storage/hal_kv.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/security/hal_crc.h"
#include "hal/serial/hal_serial.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/jh_eeprom_provider.h"
#include "hal/system/hal_sync.h"

#include <string.h>

namespace {

constexpr uint32_t KV_BANK_MAGIC = 0x564B484Au; // "JHKV", little-endian.
constexpr uint8_t KV_BANK_VERSION = 2u;
constexpr uint16_t KV_BANK_HDR_SIZE = 24u;
constexpr uint16_t KV_REC_MAGIC = 0xA55Au;
constexpr uint16_t KV_REC_FOOTER = 0x5AA5u;
constexpr uint8_t KV_REC_TYPE_U32 = 1u;
constexpr uint8_t KV_REC_TYPE_BLOB = 2u;
constexpr uint8_t KV_REC_TYPE_DELETE = 3u;
constexpr uint16_t KV_REC_HDR_SIZE = 16u;
constexpr uint16_t KV_REC_FTR_SIZE = 2u;
constexpr uint16_t KV_REC_OVERHEAD = KV_REC_HDR_SIZE + KV_REC_FTR_SIZE;
constexpr uint16_t KV_MAX_KEYS = 32u;
constexpr uint16_t KV_PUBLISH_SIZE = static_cast<uint16_t>(HAL_KV_PUBLISH_SIZE);

static_assert(HAL_KV_MAX_BANK_SIZE <= UINT16_MAX,
              "HAL_KV_MAX_BANK_SIZE must fit in uint16_t");
static_assert(HAL_KV_PUBLISH_SIZE <= UINT16_MAX,
              "HAL_KV_PUBLISH_SIZE must fit in uint16_t");
static_assert(HAL_KV_PUBLISH_SIZE >= KV_BANK_HDR_SIZE,
              "HAL_KV_PUBLISH_SIZE must contain the bank header");

struct kv_bank_hdr_t {
  uint32_t magic;
  uint8_t version;
  uint8_t reserved;
  uint16_t header_size;
  uint32_t generation;
  uint16_t used_offset;
  uint16_t bank_size;
  uint16_t record_count;
  uint16_t body_crc;
  uint16_t reserved2;
  uint16_t header_crc;
};

struct kv_rec_hdr_t {
  uint16_t magic;
  uint16_t key;
  uint8_t type;
  uint8_t flags;
  uint16_t len;
  uint32_t seq;
  uint16_t payload_crc;
  uint16_t header_crc;
};

struct kv_index_entry_t {
  bool in_use;
  uint16_t key;
  uint8_t type;
  uint16_t len;
  uint16_t payload_offset;
  uint32_t seq;
};

struct kv_bank_meta_t {
  bool valid;
  uint32_t generation;
  uint16_t used_offset;
  uint16_t record_count;
};

static hal_mutex_t s_kv_mutex = nullptr;
static bool s_ready = false;
static bool s_auto_commit = true;
static bool s_read_through = false;
static bool s_dirty = false;
static uint16_t s_base = 0u;
static uint16_t s_bank_size = 0u;
static uint16_t s_active_bank = 0u;
static uint16_t s_used_offset = 0u;
static uint16_t s_record_count = 0u;
static uint32_t s_generation = 0u;
static uint32_t s_next_seq = 1u;
static uint8_t s_bank[HAL_KV_MAX_BANK_SIZE] = {};
static kv_index_entry_t s_index[KV_MAX_KEYS] = {};

static void kv_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_kv_mutex);
}

static uint16_t read_u16(const uint8_t *raw) {
  return static_cast<uint16_t>(raw[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(raw[1]) << 8u);
}

static uint32_t read_u32(const uint8_t *raw) {
  return static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8u) |
         (static_cast<uint32_t>(raw[2]) << 16u) |
         (static_cast<uint32_t>(raw[3]) << 24u);
}

static void write_u16(uint8_t *raw, uint16_t value) {
  raw[0] = static_cast<uint8_t>(value & 0xFFu);
  raw[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
}

static void write_u32(uint8_t *raw, uint32_t value) {
  raw[0] = static_cast<uint8_t>(value & 0xFFu);
  raw[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
  raw[2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
  raw[3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
}

static uint16_t crc16(const uint8_t *data, uint16_t len) {
  return hal_crc16_ccitt(data, len, HAL_CRC16_CCITT_INIT);
}

static uint16_t bank_base(uint16_t bank) {
  return static_cast<uint16_t>(s_base + bank * s_bank_size);
}

static uint32_t record_size(uint16_t len) {
  return static_cast<uint32_t>(KV_REC_OVERHEAD) + len;
}

static bool record_type_valid(uint8_t type, uint16_t len) {
  if (type == KV_REC_TYPE_U32) {
    return len == sizeof(uint32_t);
  }
  if (type == KV_REC_TYPE_BLOB) {
    return true;
  }
  return type == KV_REC_TYPE_DELETE && len == 0u;
}

static void encode_bank_header(uint8_t raw[KV_BANK_HDR_SIZE],
                               const kv_bank_hdr_t &header) {
  write_u32(raw + 0u, header.magic);
  raw[4] = header.version;
  raw[5] = header.reserved;
  write_u16(raw + 6u, header.header_size);
  write_u32(raw + 8u, header.generation);
  write_u16(raw + 12u, header.used_offset);
  write_u16(raw + 14u, header.bank_size);
  write_u16(raw + 16u, header.record_count);
  write_u16(raw + 18u, header.body_crc);
  write_u16(raw + 20u, header.reserved2);
  write_u16(raw + 22u, header.header_crc);
}

static kv_bank_hdr_t decode_bank_header(const uint8_t *raw) {
  kv_bank_hdr_t header = {};
  header.magic = read_u32(raw + 0u);
  header.version = raw[4];
  header.reserved = raw[5];
  header.header_size = read_u16(raw + 6u);
  header.generation = read_u32(raw + 8u);
  header.used_offset = read_u16(raw + 12u);
  header.bank_size = read_u16(raw + 14u);
  header.record_count = read_u16(raw + 16u);
  header.body_crc = read_u16(raw + 18u);
  header.reserved2 = read_u16(raw + 20u);
  header.header_crc = read_u16(raw + 22u);
  return header;
}

static void encode_record_header(uint8_t raw[KV_REC_HDR_SIZE],
                                 const kv_rec_hdr_t &header) {
  write_u16(raw + 0u, header.magic);
  write_u16(raw + 2u, header.key);
  raw[4] = header.type;
  raw[5] = header.flags;
  write_u16(raw + 6u, header.len);
  write_u32(raw + 8u, header.seq);
  write_u16(raw + 12u, header.payload_crc);
  write_u16(raw + 14u, header.header_crc);
}

static kv_rec_hdr_t decode_record_header(const uint8_t *raw) {
  kv_rec_hdr_t header = {};
  header.magic = read_u16(raw + 0u);
  header.key = read_u16(raw + 2u);
  header.type = raw[4];
  header.flags = raw[5];
  header.len = read_u16(raw + 6u);
  header.seq = read_u32(raw + 8u);
  header.payload_crc = read_u16(raw + 12u);
  header.header_crc = read_u16(raw + 14u);
  return header;
}

static uint16_t record_header_crc(const kv_rec_hdr_t &header) {
  uint8_t raw[KV_REC_HDR_SIZE] = {};
  kv_rec_hdr_t copy = header;
  copy.header_crc = 0u;
  encode_record_header(raw, copy);
  return crc16(raw, KV_REC_HDR_SIZE - sizeof(uint16_t));
}

/* Validate only the publish header (magic/version/self CRC), independent of
 * any body bytes. Shared by validate_bank_buffer() (which checks the full
 * active-bank RAM copy) and hal_kv_bank_looks_present_ex() (which peeks at
 * an arbitrary candidate address without touching global KV state). */
static bool validate_bank_header(const uint8_t raw[KV_BANK_HDR_SIZE],
                                 uint16_t expected_bank_size,
                                 kv_bank_hdr_t *out_header) {
  const kv_bank_hdr_t header = decode_bank_header(raw);
  if (header.magic != KV_BANK_MAGIC || header.version != KV_BANK_VERSION ||
      header.header_size != KV_BANK_HDR_SIZE ||
      header.bank_size != expected_bank_size ||
      header.used_offset < KV_PUBLISH_SIZE ||
      header.used_offset > expected_bank_size) {
    return false;
  }

  uint8_t raw_without_crc[KV_BANK_HDR_SIZE] = {};
  kv_bank_hdr_t header_without_crc = header;
  header_without_crc.header_crc = 0u;
  encode_bank_header(raw_without_crc, header_without_crc);
  if (crc16(raw_without_crc, KV_BANK_HDR_SIZE - sizeof(uint16_t)) !=
      header.header_crc) {
    return false;
  }

  if (out_header != nullptr) {
    *out_header = header;
  }
  return true;
}

static bool validate_bank_buffer(kv_bank_meta_t *out_meta) {
  if (out_meta == nullptr) {
    return false;
  }
  *out_meta = {};

  kv_bank_hdr_t header = {};
  if (!validate_bank_header(s_bank, s_bank_size, &header)) {
    return false;
  }

  const uint16_t body_size =
      static_cast<uint16_t>(header.used_offset - KV_PUBLISH_SIZE);
  if (crc16(s_bank + KV_PUBLISH_SIZE, body_size) != header.body_crc) {
    return false;
  }

  uint16_t offset = KV_PUBLISH_SIZE;
  uint16_t records = 0u;
  while (offset < header.used_offset) {
    if (static_cast<uint32_t>(offset) + KV_REC_OVERHEAD > header.used_offset) {
      return false;
    }
    const kv_rec_hdr_t record = decode_record_header(s_bank + offset);
    if (record.magic != KV_REC_MAGIC || record.flags != 0u ||
        !record_type_valid(record.type, record.len) ||
        record.header_crc != record_header_crc(record)) {
      return false;
    }

    const uint32_t total = record_size(record.len);
    if (static_cast<uint32_t>(offset) + total > header.used_offset) {
      return false;
    }
    const uint16_t payload_offset =
        static_cast<uint16_t>(offset + KV_REC_HDR_SIZE);
    if (crc16(s_bank + payload_offset, record.len) != record.payload_crc) {
      return false;
    }
    const uint16_t footer_offset =
        static_cast<uint16_t>(payload_offset + record.len);
    if (read_u16(s_bank + footer_offset) != KV_REC_FOOTER) {
      return false;
    }
    offset = static_cast<uint16_t>(offset + total);
    records++;
  }

  if (offset != header.used_offset || records != header.record_count) {
    return false;
  }
  out_meta->valid = true;
  out_meta->generation = header.generation;
  out_meta->used_offset = header.used_offset;
  out_meta->record_count = header.record_count;
  return true;
}

static int index_find(uint16_t key) {
  for (uint16_t i = 0u; i < KV_MAX_KEYS; i++) {
    if (s_index[i].in_use && s_index[i].key == key) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static int index_alloc(void) {
  for (uint16_t i = 0u; i < KV_MAX_KEYS; i++) {
    if (!s_index[i].in_use) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static uint16_t index_count(void) {
  uint16_t count = 0u;
  for (uint16_t i = 0u; i < KV_MAX_KEYS; i++) {
    if (s_index[i].in_use) {
      count++;
    }
  }
  return count;
}

static hal_status_t build_index_from_staging(void) {
  memset(s_index, 0, sizeof(s_index));
  uint16_t offset = KV_PUBLISH_SIZE;
  uint32_t last_seq = 0u;
  while (offset < s_used_offset) {
    const kv_rec_hdr_t record = decode_record_header(s_bank + offset);
    int index = index_find(record.key);
    if (record.type == KV_REC_TYPE_DELETE) {
      if (index >= 0) {
        s_index[index].in_use = false;
      }
    } else {
      if (index < 0) {
        index = index_alloc();
      }
      if (index < 0) {
        hal_derr("hal_kv: key index full, increase KV_MAX_KEYS");
        return HAL_ENOMEM;
      }
      s_index[index].in_use = true;
      s_index[index].key = record.key;
      s_index[index].type = record.type;
      s_index[index].len = record.len;
      s_index[index].payload_offset =
          static_cast<uint16_t>(offset + KV_REC_HDR_SIZE);
      s_index[index].seq = record.seq;
    }
    last_seq = record.seq;
    offset = static_cast<uint16_t>(offset + record_size(record.len));
  }
  s_next_seq = last_seq + 1u;
  return HAL_OK;
}

static void prepare_bank_header(uint32_t generation) {
  memset(s_bank, 0xFF, KV_PUBLISH_SIZE);
  if (s_used_offset < s_bank_size) {
    memset(s_bank + s_used_offset, 0xFF, s_bank_size - s_used_offset);
  }
  kv_bank_hdr_t header = {};
  header.magic = KV_BANK_MAGIC;
  header.version = KV_BANK_VERSION;
  header.header_size = KV_BANK_HDR_SIZE;
  header.generation = generation;
  header.used_offset = s_used_offset;
  header.bank_size = s_bank_size;
  header.record_count = s_record_count;
  header.body_crc =
      crc16(s_bank + KV_PUBLISH_SIZE,
            static_cast<uint16_t>(s_used_offset - KV_PUBLISH_SIZE));
  uint8_t raw[KV_BANK_HDR_SIZE] = {};
  encode_bank_header(raw, header);
  header.header_crc = crc16(raw, KV_BANK_HDR_SIZE - sizeof(uint16_t));
  encode_bank_header(s_bank, header);
}

static hal_status_t publish_locked(void) {
  if (!s_dirty) {
    return HAL_OK;
  }
  const uint16_t destination = s_active_bank == 0u ? 1u : 0u;
  const uint32_t next_generation = s_generation + 1u;
  prepare_bank_header(next_generation);
  const hal_status_t status = jh_eeprom_replace_region(
      bank_base(destination), s_bank, s_bank_size, KV_PUBLISH_SIZE);
  if (hal_status_is_error(status)) {
    return status;
  }
  s_active_bank = destination;
  s_generation = next_generation;
  s_dirty = false;
  return HAL_OK;
}

static hal_status_t finish_mutation_locked(void) {
  s_dirty = true;
  return s_auto_commit ? publish_locked() : HAL_OK;
}

static hal_status_t finish_no_change_locked(void) {
  return s_auto_commit && s_dirty ? publish_locked() : HAL_OK;
}

static hal_status_t append_record_locked(uint16_t key, uint8_t type,
                                         const uint8_t *data, uint16_t len) {
  const uint32_t total = record_size(len);
  if (static_cast<uint32_t>(s_used_offset) + total > s_bank_size) {
    return HAL_ENOMEM;
  }
  const uint16_t record_offset = s_used_offset;
  const uint16_t payload_offset =
      static_cast<uint16_t>(record_offset + KV_REC_HDR_SIZE);
  kv_rec_hdr_t record = {};
  record.magic = KV_REC_MAGIC;
  record.key = key;
  record.type = type;
  record.len = len;
  record.seq = s_next_seq++;
  record.payload_crc = crc16(data, len);
  record.header_crc = record_header_crc(record);
  encode_record_header(s_bank + record_offset, record);
  if (len > 0u) {
    memcpy(s_bank + payload_offset, data, len);
  }
  write_u16(s_bank + payload_offset + len, KV_REC_FOOTER);
  s_used_offset = static_cast<uint16_t>(s_used_offset + total);
  s_record_count++;
  return HAL_OK;
}

static hal_status_t compact_locked(void) {
  uint16_t order[KV_MAX_KEYS] = {};
  uint16_t live_count = 0u;
  for (uint16_t i = 0u; i < KV_MAX_KEYS; i++) {
    if (s_index[i].in_use) {
      order[live_count++] = i;
    }
  }
  for (uint16_t i = 1u; i < live_count; i++) {
    const uint16_t item = order[i];
    uint16_t pos = i;
    while (pos > 0u && s_index[order[pos - 1u]].payload_offset >
                           s_index[item].payload_offset) {
      order[pos] = order[pos - 1u];
      pos--;
    }
    order[pos] = item;
  }

  uint16_t destination = KV_PUBLISH_SIZE;
  for (uint16_t order_index = 0u; order_index < live_count; order_index++) {
    kv_index_entry_t &entry = s_index[order[order_index]];
    const uint16_t source_payload = entry.payload_offset;
    const uint16_t destination_payload =
        static_cast<uint16_t>(destination + KV_REC_HDR_SIZE);
    memmove(s_bank + destination_payload, s_bank + source_payload, entry.len);

    kv_rec_hdr_t record = {};
    record.magic = KV_REC_MAGIC;
    record.key = entry.key;
    record.type = entry.type;
    record.len = entry.len;
    record.seq = entry.seq;
    record.payload_crc = crc16(s_bank + destination_payload, entry.len);
    record.header_crc = record_header_crc(record);
    encode_record_header(s_bank + destination, record);
    write_u16(s_bank + destination_payload + entry.len, KV_REC_FOOTER);
    entry.payload_offset = destination_payload;
    destination = static_cast<uint16_t>(destination + record_size(entry.len));
  }
  s_used_offset = destination;
  s_record_count = live_count;
  s_dirty = true;
  return HAL_OK;
}

static hal_status_t ensure_space_locked(uint16_t len) {
  const uint32_t required = record_size(len);
  if (static_cast<uint32_t>(s_used_offset) + required <= s_bank_size) {
    return HAL_OK;
  }
  const hal_status_t status = compact_locked();
  if (hal_status_is_error(status)) {
    return status;
  }
  return static_cast<uint32_t>(s_used_offset) + required <= s_bank_size
             ? HAL_OK
             : HAL_ENOMEM;
}

static hal_status_t set_blob_locked(uint16_t key, uint8_t type,
                                    const uint8_t *data, uint16_t len) {
  if (record_size(len) + KV_PUBLISH_SIZE > s_bank_size) {
    hal_derr("hal_kv_set_blob: value too large (%u)",
             static_cast<unsigned>(len));
    return HAL_EOVERFLOW;
  }

  int index = index_find(key);
  if (index >= 0 && s_index[index].type == type && s_index[index].len == len &&
      (len == 0u ||
       memcmp(s_bank + s_index[index].payload_offset, data, len) == 0)) {
    return finish_no_change_locked();
  }
  if (index < 0 && index_alloc() < 0) {
    hal_derr("hal_kv: key index full, increase KV_MAX_KEYS");
    return HAL_ENOMEM;
  }

  const hal_status_t space_status = ensure_space_locked(len);
  if (hal_status_is_error(space_status)) {
    return space_status;
  }
  const uint16_t payload_offset =
      static_cast<uint16_t>(s_used_offset + KV_REC_HDR_SIZE);
  const hal_status_t append_status = append_record_locked(key, type, data, len);
  if (hal_status_is_error(append_status)) {
    return append_status;
  }
  index = index_find(key);
  if (index < 0) {
    index = index_alloc();
  }
  s_index[index].in_use = true;
  s_index[index].key = key;
  s_index[index].type = type;
  s_index[index].len = len;
  s_index[index].payload_offset = payload_offset;
  s_index[index].seq = s_next_seq - 1u;
  return finish_mutation_locked();
}

static hal_status_t delete_locked(uint16_t key) {
  const int index = index_find(key);
  if (index < 0) {
    return finish_no_change_locked();
  }
  const hal_status_t space_status = ensure_space_locked(0u);
  if (hal_status_is_error(space_status)) {
    return space_status;
  }
  const hal_status_t append_status =
      append_record_locked(key, KV_REC_TYPE_DELETE, nullptr, 0u);
  if (hal_status_is_error(append_status)) {
    return append_status;
  }
  s_index[index].in_use = false;
  return finish_mutation_locked();
}

static bool generation_newer(uint32_t lhs, uint32_t rhs) {
  return static_cast<int32_t>(lhs - rhs) > 0;
}

static void reset_runtime_state_locked(void) {
  s_ready = false;
  s_dirty = false;
  s_base = 0u;
  s_bank_size = 0u;
  s_active_bank = 0u;
  s_used_offset = 0u;
  s_record_count = 0u;
  s_generation = 0u;
  s_next_seq = 1u;
  memset(s_bank, 0, sizeof(s_bank));
  memset(s_index, 0, sizeof(s_index));
}

} // namespace

hal_status_t hal_kv_init_ex(uint16_t base_addr, uint16_t size_bytes) {
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  reset_runtime_state_locked();

  if (size_bytes == 0u || (size_bytes & 1u) != 0u) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EINVAL;
  }
  const uint16_t bank_size = static_cast<uint16_t>(size_bytes / 2u);
  if (bank_size > HAL_KV_MAX_BANK_SIZE ||
      bank_size < static_cast<uint16_t>(KV_PUBLISH_SIZE + KV_REC_OVERHEAD)) {
    hal_derr("hal_kv_init: invalid bank size (%u)",
             static_cast<unsigned>(bank_size));
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EINVAL;
  }

  uint16_t eeprom_size = 0u;
  hal_status_t status = hal_eeprom_size_ex(&eeprom_size);
  if (hal_status_is_error(status)) {
    hal_mutex_unlock(s_kv_mutex);
    return status;
  }
  if (static_cast<uint32_t>(base_addr) + size_bytes > eeprom_size) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EOVERFLOW;
  }

  s_base = base_addr;
  s_bank_size = bank_size;
  kv_bank_meta_t metadata[2] = {};
  for (uint16_t bank = 0u; bank < 2u; bank++) {
    status = hal_eeprom_read_bytes(bank_base(bank), s_bank, s_bank_size);
    if (hal_status_is_error(status)) {
      hal_mutex_unlock(s_kv_mutex);
      return status;
    }
    (void)validate_bank_buffer(&metadata[bank]);
  }

  if (!metadata[0].valid && !metadata[1].valid) {
    memset(s_bank, 0xFF, s_bank_size);
    memset(s_index, 0, sizeof(s_index));
    s_active_bank = 1u;
    s_used_offset = KV_PUBLISH_SIZE;
    s_record_count = 0u;
    s_generation = 0u;
    s_next_seq = 1u;
    s_dirty = true;
    status = publish_locked();
    if (hal_status_is_error(status)) {
      reset_runtime_state_locked();
      hal_mutex_unlock(s_kv_mutex);
      return status;
    }
  } else {
    s_active_bank =
        metadata[0].valid && (!metadata[1].valid ||
                              !generation_newer(metadata[1].generation,
                                                metadata[0].generation))
            ? 0u
            : 1u;
    const kv_bank_meta_t &active = metadata[s_active_bank];
    status =
        hal_eeprom_read_bytes(bank_base(s_active_bank), s_bank, s_bank_size);
    if (hal_status_is_error(status)) {
      reset_runtime_state_locked();
      hal_mutex_unlock(s_kv_mutex);
      return status;
    }
    kv_bank_meta_t verified = {};
    if (!validate_bank_buffer(&verified)) {
      reset_runtime_state_locked();
      hal_mutex_unlock(s_kv_mutex);
      return HAL_EIO;
    }
    s_generation = active.generation;
    s_used_offset = active.used_offset;
    s_record_count = active.record_count;
    status = build_index_from_staging();
    if (hal_status_is_error(status)) {
      reset_runtime_state_locked();
      hal_mutex_unlock(s_kv_mutex);
      return status;
    }
  }

  s_ready = true;
  s_dirty = false;
  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

bool hal_kv_init(uint16_t base_addr, uint16_t size_bytes) {
  return hal_status_to_bool(hal_kv_init_ex(base_addr, size_bytes));
}

hal_status_t hal_kv_set_u32_ex(uint16_t key, uint32_t value) {
  uint8_t raw[sizeof(uint32_t)] = {};
  write_u32(raw, value);
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
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
  if (out_value == nullptr) {
    hal_derr("hal_kv_get_u32: out_value is NULL");
    return HAL_EINVAL;
  }
  *out_value = 0u;
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  if (!s_ready) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EUNINIT;
  }
  const int index = index_find(key);
  if (index < 0 || s_index[index].type != KV_REC_TYPE_U32 ||
      s_index[index].len != sizeof(uint32_t)) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_ENOENT;
  }
  if (s_read_through) {
    uint8_t raw[sizeof(uint32_t)] = {};
    const uint16_t addr = static_cast<uint16_t>(bank_base(s_active_bank) +
                                                s_index[index].payload_offset);
    const hal_status_t status = hal_eeprom_read_bytes(addr, raw, sizeof(raw));
    hal_mutex_unlock(s_kv_mutex);
    if (hal_status_is_error(status)) {
      return status;
    }
    *out_value = read_u32(raw);
    return HAL_OK;
  }
  *out_value = read_u32(s_bank + s_index[index].payload_offset);
  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

bool hal_kv_get_u32(uint16_t key, uint32_t *out_value) {
  return hal_status_to_bool(hal_kv_get_u32_ex(key, out_value));
}

hal_status_t hal_kv_set_blob_ex(uint16_t key, const uint8_t *data,
                                uint16_t len) {
  if (len > 0u && data == nullptr) {
    hal_derr("hal_kv_set_blob: data is NULL while len > 0");
    return HAL_EINVAL;
  }
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
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
  if (out_len != nullptr) {
    *out_len = 0u;
  }
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  if (!s_ready) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EUNINIT;
  }
  const int index = index_find(key);
  if (index < 0 || s_index[index].type != KV_REC_TYPE_BLOB) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_ENOENT;
  }
  if (out_len != nullptr) {
    *out_len = s_index[index].len;
  }
  if (out == nullptr) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_OK;
  }
  if (out_size < s_index[index].len) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EOVERFLOW;
  }
  if (s_read_through) {
    hal_status_t status = HAL_OK;
    if (s_index[index].len > 0u) {
      const uint16_t addr = static_cast<uint16_t>(
          bank_base(s_active_bank) + s_index[index].payload_offset);
      status = hal_eeprom_read_bytes(addr, out, s_index[index].len);
    }
    hal_mutex_unlock(s_kv_mutex);
    return status;
  }
  if (s_index[index].len > 0u) {
    memcpy(out, s_bank + s_index[index].payload_offset, s_index[index].len);
  }
  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

bool hal_kv_get_blob(uint16_t key, uint8_t *out, uint16_t out_size,
                     uint16_t *out_len) {
  return hal_status_to_bool(hal_kv_get_blob_ex(key, out, out_size, out_len));
}

hal_status_t hal_kv_delete_ex(uint16_t key) {
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
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
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  hal_status_t status = HAL_EUNINIT;
  if (s_ready) {
    status = compact_locked();
    if (hal_status_is_ok(status) && s_auto_commit) {
      status = publish_locked();
    }
  }
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_gc(void) { return hal_status_to_bool(hal_kv_gc_ex()); }

hal_status_t hal_kv_get_stats_ex(hal_kv_stats_t *out_stats) {
  if (out_stats == nullptr) {
    hal_derr("hal_kv_get_stats: out_stats is NULL");
    return HAL_EINVAL;
  }
  *out_stats = {};
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  if (!s_ready) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EUNINIT;
  }
  out_stats->generation = s_generation;
  out_stats->used_bytes = s_used_offset;
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
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  s_auto_commit = enabled;
  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

hal_status_t hal_kv_set_read_through(bool enabled) {
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  s_read_through = enabled;
  hal_mutex_unlock(s_kv_mutex);
  return HAL_OK;
}

hal_status_t hal_kv_bank_looks_present_ex(uint16_t bank_addr,
                                          uint16_t bank_size,
                                          bool *out_present) {
  if (out_present == nullptr) {
    return HAL_EINVAL;
  }
  *out_present = false;
  if (bank_size < KV_PUBLISH_SIZE) {
    return HAL_EINVAL;
  }

  uint8_t header_raw[KV_BANK_HDR_SIZE] = {};
  const hal_status_t status =
      hal_eeprom_read_bytes(bank_addr, header_raw, KV_BANK_HDR_SIZE);
  if (hal_status_is_error(status)) {
    return status;
  }
  *out_present = validate_bank_header(header_raw, bank_size, nullptr);
  return HAL_OK;
}

bool hal_kv_bank_looks_present(uint16_t bank_addr, uint16_t bank_size) {
  bool present = false;
  (void)hal_kv_bank_looks_present_ex(bank_addr, bank_size, &present);
  return present;
}

hal_status_t hal_kv_commit_ex(void) {
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  const hal_status_t status = s_ready ? publish_locked() : HAL_EUNINIT;
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_commit(void) { return hal_status_to_bool(hal_kv_commit_ex()); }

#if HAL_TARGET_IS_MOCK
void hal_mock_kv_full_reset(void) {
  if (s_kv_mutex != nullptr) {
    hal_mutex_lock(s_kv_mutex);
    reset_runtime_state_locked();
    s_auto_commit = true;
    s_read_through = false;
    hal_mutex_unlock(s_kv_mutex);
    hal_mutex_destroy(s_kv_mutex);
    s_kv_mutex = nullptr;
  }
}
#endif /* HAL_TARGET_IS_MOCK */

#endif /* HAL_ENABLE_KV */
