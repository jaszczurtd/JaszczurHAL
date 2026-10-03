#include "hal/core/hal_config.h"
#ifdef HAL_ENABLE_KV

#include "hal/storage/hal_kv.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_endian.h"
#include "hal/security/hal_crc.h"
#include "hal/serial/hal_serial.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/jh_eeprom_provider.h"
#include "hal/system/hal_sync.h"

#include <string.h>

namespace {

constexpr uint32_t KV_BANK_MAGIC = 0x564B484Au; // "JHKV", little-endian.
/* Version 3 adds the committed log after the body; a version 2 bank is the
 * same layout with an empty log, so both are read. */
constexpr uint8_t KV_BANK_VERSION = 3u;
constexpr uint8_t KV_BANK_VERSION_NO_LOG = 2u;
constexpr uint16_t KV_BANK_HDR_SIZE = 24u;
constexpr uint16_t KV_REC_MAGIC = 0xA55Au;
constexpr uint16_t KV_REC_FOOTER = 0x5AA5u;
constexpr uint8_t KV_REC_TYPE_U32 = 1u;
constexpr uint8_t KV_REC_TYPE_BLOB = 2u;
constexpr uint8_t KV_REC_TYPE_DELETE = 3u;
/* Closes a log batch: payload = u16 batch start, u16 CRC of the batch. */
constexpr uint8_t KV_REC_TYPE_COMMIT = 4u;
constexpr uint16_t KV_COMMIT_LEN = 4u;
constexpr uint16_t KV_REC_HDR_SIZE = 16u;
constexpr uint16_t KV_REC_FTR_SIZE = 2u;
constexpr uint16_t KV_REC_OVERHEAD = KV_REC_HDR_SIZE + KV_REC_FTR_SIZE;
constexpr uint16_t KV_MAX_KEYS = static_cast<uint16_t>(HAL_KV_MAX_KEYS);
constexpr uint16_t KV_PUBLISH_SIZE = static_cast<uint16_t>(HAL_KV_PUBLISH_SIZE);

static_assert(HAL_KV_MAX_BANK_SIZE <= UINT16_MAX,
              "HAL_KV_MAX_BANK_SIZE must fit in uint16_t");
static_assert(HAL_KV_MAX_KEYS >= 1u && HAL_KV_MAX_KEYS <= 4096u,
              "HAL_KV_MAX_KEYS must be within 1..4096");
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
/* The next commit must compact into the other bank instead of appending. */
static bool s_needs_publish = false;
static uint16_t s_append_size = 1u;
/* Active-bank offset up to which the medium holds committed content; the
 * next log batch starts here. RAM records past it are not committed yet. */
static uint16_t s_flash_end = 0u;
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

static uint16_t crc16(const uint8_t *data, uint16_t len) {
  return hal_crc16_ccitt(data, len, HAL_CRC16_CCITT_INIT);
}

static uint16_t bank_base(uint16_t bank) {
  return static_cast<uint16_t>(s_base + bank * s_bank_size);
}

static uint32_t record_size(uint16_t len) {
  return static_cast<uint32_t>(KV_REC_OVERHEAD) + len;
}

static uint32_t align_up(uint32_t value) {
  return (value + s_append_size - 1u) / s_append_size * s_append_size;
}

static bool bytes_erased(uint16_t offset, uint16_t len) {
  for (uint16_t index = 0u; index < len; index++) {
    if (s_bank[offset + index] != 0xFFu) {
      return false;
    }
  }
  return true;
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
  jh_store_le32(raw + 0u, header.magic);
  raw[4] = header.version;
  raw[5] = header.reserved;
  jh_store_le16(raw + 6u, header.header_size);
  jh_store_le32(raw + 8u, header.generation);
  jh_store_le16(raw + 12u, header.used_offset);
  jh_store_le16(raw + 14u, header.bank_size);
  jh_store_le16(raw + 16u, header.record_count);
  jh_store_le16(raw + 18u, header.body_crc);
  jh_store_le16(raw + 20u, header.reserved2);
  jh_store_le16(raw + 22u, header.header_crc);
}

static kv_bank_hdr_t decode_bank_header(const uint8_t *raw) {
  kv_bank_hdr_t header = {};
  header.magic = jh_load_le32(raw + 0u);
  header.version = raw[4];
  header.reserved = raw[5];
  header.header_size = jh_load_le16(raw + 6u);
  header.generation = jh_load_le32(raw + 8u);
  header.used_offset = jh_load_le16(raw + 12u);
  header.bank_size = jh_load_le16(raw + 14u);
  header.record_count = jh_load_le16(raw + 16u);
  header.body_crc = jh_load_le16(raw + 18u);
  header.reserved2 = jh_load_le16(raw + 20u);
  header.header_crc = jh_load_le16(raw + 22u);
  return header;
}

static void encode_record_header(uint8_t raw[KV_REC_HDR_SIZE],
                                 const kv_rec_hdr_t &header) {
  jh_store_le16(raw + 0u, header.magic);
  jh_store_le16(raw + 2u, header.key);
  raw[4] = header.type;
  raw[5] = header.flags;
  jh_store_le16(raw + 6u, header.len);
  jh_store_le32(raw + 8u, header.seq);
  jh_store_le16(raw + 12u, header.payload_crc);
  jh_store_le16(raw + 14u, header.header_crc);
}

static kv_rec_hdr_t decode_record_header(const uint8_t *raw) {
  kv_rec_hdr_t header = {};
  header.magic = jh_load_le16(raw + 0u);
  header.key = jh_load_le16(raw + 2u);
  header.type = raw[4];
  header.flags = raw[5];
  header.len = jh_load_le16(raw + 6u);
  header.seq = jh_load_le32(raw + 8u);
  header.payload_crc = jh_load_le16(raw + 12u);
  header.header_crc = jh_load_le16(raw + 14u);
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
  if (header.magic != KV_BANK_MAGIC ||
      (header.version != KV_BANK_VERSION &&
       header.version != KV_BANK_VERSION_NO_LOG) ||
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

/* Check one stored record at offset that must end by limit; a commit record
 * is accepted only where allow_commit says so (the log, not the body). */
static bool record_valid(uint16_t offset, uint16_t limit, bool allow_commit,
                         kv_rec_hdr_t *out_record) {
  if (static_cast<uint32_t>(offset) + KV_REC_OVERHEAD > limit) {
    return false;
  }
  const kv_rec_hdr_t record = decode_record_header(s_bank + offset);
  const bool commit = record.type == KV_REC_TYPE_COMMIT &&
                      record.len == KV_COMMIT_LEN && allow_commit;
  if (record.magic != KV_REC_MAGIC || record.flags != 0u ||
      (!commit && !record_type_valid(record.type, record.len)) ||
      record.header_crc != record_header_crc(record) ||
      static_cast<uint32_t>(offset) + record_size(record.len) > limit) {
    return false;
  }
  const uint16_t payload_offset =
      static_cast<uint16_t>(offset + KV_REC_HDR_SIZE);
  if (crc16(s_bank + payload_offset, record.len) != record.payload_crc ||
      jh_load_le16(s_bank + payload_offset + record.len) != KV_REC_FOOTER) {
    return false;
  }
  *out_record = record;
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
    kv_rec_hdr_t record = {};
    if (!record_valid(offset, header.used_offset, false, &record)) {
      return false;
    }
    offset = static_cast<uint16_t>(offset + record_size(record.len));
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

/* Apply the validated records of [offset, end) to the index in order. */
static hal_status_t index_apply_range(uint16_t offset, uint16_t end) {
  while (offset < end) {
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
        hal_derr("hal_kv: key index full (%u keys), raise HAL_KV_MAX_KEYS",
                 static_cast<unsigned>(KV_MAX_KEYS));
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
    s_next_seq = record.seq + 1u;
    offset = static_cast<uint16_t>(offset + record_size(record.len));
  }
  return HAL_OK;
}

/*
 * Walk the log after the body: batches of records, each closed by a commit
 * record whose CRC covers the batch, the next batch starting on the append
 * granularity. A batch counts only with its commit; the log ends at erased
 * bytes. Anything else past the last commit (a write cut by power loss) is
 * left alone, and the next commit compacts into the other bank instead.
 */
static hal_status_t scan_log_locked(uint16_t body_end) {
  uint16_t committed = body_end;
  uint16_t batch = static_cast<uint16_t>(align_up(body_end));
  uint16_t offset = batch;
  bool damaged = false;
  while (batch < s_bank_size) {
    kv_rec_hdr_t record = {};
    if (bytes_erased(offset, static_cast<uint16_t>(s_bank_size - offset))) {
      damaged = offset != batch;
      break;
    }
    if (!record_valid(offset, s_bank_size, true, &record)) {
      damaged = true;
      break;
    }
    const uint16_t next =
        static_cast<uint16_t>(offset + record_size(record.len));
    if (record.type != KV_REC_TYPE_COMMIT) {
      offset = next;
      continue;
    }
    const uint8_t *payload = s_bank + offset + KV_REC_HDR_SIZE;
    if (jh_load_le16(payload) != batch || offset == batch ||
        jh_load_le16(payload + 2u) !=
            crc16(s_bank + batch, static_cast<uint16_t>(offset - batch))) {
      damaged = true;
      break;
    }
    const hal_status_t status = index_apply_range(batch, offset);
    if (status != HAL_OK) {
      return status;
    }
    committed = next;
    batch = static_cast<uint16_t>(align_up(next));
    offset = batch;
  }
  s_flash_end = static_cast<uint16_t>(align_up(committed));
  s_used_offset = damaged ? committed : s_flash_end;
  s_needs_publish = damaged;
  return HAL_OK;
}

/* Encode one record at offset; data may already sit at its payload offset
 * (compaction moves payloads left, so the payload goes first). */
static void write_record(uint16_t offset, uint16_t key, uint8_t type,
                         const uint8_t *data, uint16_t len, uint32_t seq) {
  const uint16_t payload_offset =
      static_cast<uint16_t>(offset + KV_REC_HDR_SIZE);
  if (len > 0u) {
    memmove(s_bank + payload_offset, data, len);
  }
  kv_rec_hdr_t record = {};
  record.magic = KV_REC_MAGIC;
  record.key = key;
  record.type = type;
  record.len = len;
  record.seq = seq;
  record.payload_crc = crc16(s_bank + payload_offset, len);
  record.header_crc = record_header_crc(record);
  encode_record_header(s_bank + offset, record);
  jh_store_le16(s_bank + payload_offset + len, KV_REC_FOOTER);
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

/* Write the compacted RAM image to the other bank, its header last. The
 * erase is skipped when hal_kv_prepare_ex() already erased that bank. */
static hal_status_t publish_full_locked(void) {
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
  s_flash_end = static_cast<uint16_t>(align_up(s_used_offset));
  s_used_offset = s_flash_end;
  s_needs_publish = false;
  s_dirty = false;
  return HAL_OK;
}

static uint32_t commit_record_reserve(void) {
  return s_needs_publish ? 0u : record_size(KV_COMMIT_LEN) + s_append_size - 1u;
}

/* Close the records staged since the last commit with a commit record and
 * program them after the committed log, without any erase. */
static hal_status_t append_batch_locked(void) {
  const uint16_t batch = s_flash_end;
  const uint16_t commit = s_used_offset;
  const uint32_t end = align_up(commit + record_size(KV_COMMIT_LEN));
  if (end > s_bank_size) {
    return HAL_ENOMEM;
  }
  uint8_t payload[KV_COMMIT_LEN] = {};
  jh_store_le16(payload, batch);
  jh_store_le16(payload + 2u,
                crc16(s_bank + batch, static_cast<uint16_t>(commit - batch)));
  write_record(commit, 0u, KV_REC_TYPE_COMMIT, payload, KV_COMMIT_LEN,
               s_next_seq);
  const hal_status_t status = jh_eeprom_append_region(
      static_cast<uint16_t>(bank_base(s_active_bank) + batch), s_bank + batch,
      static_cast<uint16_t>(end - batch));
  if (hal_status_is_error(status)) {
    memset(s_bank + commit, 0xFF, end - commit);
    /* A refused write leaves the tail erased and is retried in place; once
     * part of the batch reached the medium, never append there again. */
    bool erased = false;
    if (jh_eeprom_region_erased(
            static_cast<uint16_t>(bank_base(s_active_bank) + batch),
            static_cast<uint16_t>(end - batch), &erased) != HAL_OK ||
        !erased) {
      s_needs_publish = true;
    }
    return status;
  }
  s_flash_end = static_cast<uint16_t>(end);
  s_used_offset = s_flash_end;
  s_dirty = false;
  return HAL_OK;
}

static hal_status_t compact_locked(void);

static hal_status_t commit_locked(void) {
  if (!s_dirty) {
    return HAL_OK;
  }
  if (!s_needs_publish) {
    if (s_used_offset == s_flash_end) {
      s_dirty = false;
      return HAL_OK;
    }
    /* No room for the batch, or bytes past the log that the store did not
     * write (nothing was programmed then): compact instead. */
    const hal_status_t status = append_batch_locked();
    if (status != HAL_ENOMEM && status != HAL_ESTATE) {
      return status;
    }
  }
  const hal_status_t status = compact_locked();
  return hal_status_is_error(status) ? status : publish_full_locked();
}

static hal_status_t finish_mutation_locked(void) {
  s_dirty = true;
  return s_auto_commit ? commit_locked() : HAL_OK;
}

static hal_status_t finish_no_change_locked(void) {
  return s_auto_commit && s_dirty ? commit_locked() : HAL_OK;
}

static hal_status_t append_record_locked(uint16_t key, uint8_t type,
                                         const uint8_t *data, uint16_t len) {
  const uint32_t total = record_size(len);
  if (static_cast<uint32_t>(s_used_offset) + total > s_bank_size) {
    return HAL_ENOMEM;
  }
  write_record(s_used_offset, key, type, data, len, s_next_seq++);
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
    write_record(destination, entry.key, entry.type,
                 s_bank + entry.payload_offset, entry.len, entry.seq);
    entry.payload_offset = static_cast<uint16_t>(destination + KV_REC_HDR_SIZE);
    destination = static_cast<uint16_t>(destination + record_size(entry.len));
  }
  s_used_offset = destination;
  s_record_count = live_count;
  s_needs_publish = true;
  s_dirty = true;
  return HAL_OK;
}

static hal_status_t ensure_space_locked(uint16_t len) {
  const uint32_t required = record_size(len);
  if (static_cast<uint32_t>(s_used_offset) + required +
          commit_record_reserve() <=
      s_bank_size) {
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
    hal_derr("hal_kv: key index full (%u keys), raise HAL_KV_MAX_KEYS",
             static_cast<unsigned>(KV_MAX_KEYS));
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
  s_needs_publish = false;
  s_append_size = 1u;
  s_flash_end = 0u;
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

  status = jh_eeprom_validate_region(base_addr, bank_size, KV_PUBLISH_SIZE);
  if (status == HAL_OK) {
    status = jh_eeprom_append_size(&s_append_size);
  }
  if (status != HAL_OK) {
    hal_mutex_unlock(s_kv_mutex);
    return status;
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
    s_needs_publish = true;
    status = commit_locked();
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
    memset(s_index, 0, sizeof(s_index));
    s_next_seq = 1u;
    status = index_apply_range(KV_PUBLISH_SIZE, active.used_offset);
    if (status == HAL_OK) {
      status = scan_log_locked(active.used_offset);
    }
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
  jh_store_le32(raw, value);
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
  if (s_read_through && s_dirty) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EBUSY;
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
    *out_value = jh_load_le32(raw);
    return HAL_OK;
  }
  *out_value = jh_load_le32(s_bank + s_index[index].payload_offset);
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
  if (s_read_through && s_dirty) {
    hal_mutex_unlock(s_kv_mutex);
    return HAL_EBUSY;
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
      status = commit_locked();
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
  out_stats->key_capacity = KV_MAX_KEYS;
  out_stats->next_sequence = s_next_seq;
  const uint16_t spare = s_active_bank == 0u ? 1u : 0u;
  const hal_status_t status = jh_eeprom_region_erased(
      bank_base(spare), s_bank_size, &out_stats->spare_erased);
  hal_mutex_unlock(s_kv_mutex);
  return status;
}

bool hal_kv_get_stats(hal_kv_stats_t *out_stats) {
  return hal_status_to_bool(hal_kv_get_stats_ex(out_stats));
}

hal_status_t hal_kv_prepare_ex(void) {
  kv_ensure_mutex();
  if (s_kv_mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_kv_mutex);
  hal_status_t status = HAL_EUNINIT;
  if (s_ready) {
    const uint16_t spare = bank_base(s_active_bank == 0u ? 1u : 0u);
    bool erased = false;
    status = jh_eeprom_region_erased(spare, s_bank_size, &erased);
    if (status == HAL_OK && !erased) {
      status = jh_eeprom_erase_region(spare, s_bank_size);
    }
  }
  hal_mutex_unlock(s_kv_mutex);
  return status;
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
  const hal_status_t status = s_ready ? commit_locked() : HAL_EUNINIT;
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
