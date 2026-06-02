#include <JaszczurHAL.h>

static const uint16_t EEPROM_SIZE_BYTES = 1024;
static const uint16_t KV_BASE_ADDR = 0;
static const uint16_t KV_SIZE_BYTES = 512;

static const uint16_t KEY_BOOT_COUNT = 1;
static const uint16_t KEY_DEVICE_NAME = 2;

static uint32_t last_stats_ms = 0;

static void printStats(void) {
  hal_kv_stats_t stats = {};
  if (!hal_kv_get_stats(&stats)) {
    hal_derr("KV: stats unavailable");
    return;
  }

  hal_deb("KV: gen=%lu used=%u/%u keys=%u next=%lu",
          (unsigned long)stats.generation,
          stats.used_bytes,
          stats.capacity_bytes,
          stats.key_count,
          (unsigned long)stats.next_sequence);
}

static void initKvStore(void) {
  hal_eeprom_init(HAL_EEPROM_RP2040, EEPROM_SIZE_BYTES, 0);
  if (!hal_kv_init(KV_BASE_ADDR, KV_SIZE_BYTES)) {
    hal_derr("KV: init failed");
    return;
  }

  hal_kv_set_auto_commit(false);

  uint32_t boot_count = 0;
  if (!hal_kv_get_u32(KEY_BOOT_COUNT, &boot_count)) {
    boot_count = 0;
  }
  boot_count++;

  const char device_name[] = "jaszczurhal-kv";
  hal_kv_set_u32(KEY_BOOT_COUNT, boot_count);
  hal_kv_set_blob(KEY_DEVICE_NAME,
                  (const uint8_t *)device_name,
                  (uint16_t)sizeof(device_name));
  hal_kv_commit();

  hal_deb("KV: boot_count=%lu", (unsigned long)boot_count);

  uint8_t name_buf[32] = {0};
  uint16_t name_len = 0;
  if (hal_kv_get_blob(KEY_DEVICE_NAME, name_buf, sizeof(name_buf), &name_len)) {
    hal_deb("KV: device_name=%s len=%u", (const char *)name_buf, name_len);
  }

  printStats();
}

void setup() {
  hal_debug_init(115200);
  initKvStore();
}

void loop() {
  const uint32_t now = hal_millis();
  if (now - last_stats_ms < 10000u) {
    return;
  }
  last_stats_ms = now;

  printStats();
}
