#include "utils/unity.h"

extern "C" {
#include "hal/storage/filesystem/ff16/diskio.h"
#include "hal/storage/filesystem/ff16/ff.h"
}

#include <stdint.h>
#include <string.h>

#define MEMDISK_SECTOR_SIZE 512u
#define MEMDISK_SECTOR_COUNT 8192u
#define MEMDISK_FAT_SECTORS 32u
#define MEMDISK_ROOT_ENTRIES 512u

static uint8_t s_disk[MEMDISK_SECTOR_COUNT][MEMDISK_SECTOR_SIZE];
static FATFS s_fs;
static bool s_initialized;

static void store_le16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
}

static void store_le32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)value;
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

static void init_fat16_image(void) {
  memset(s_disk, 0, sizeof(s_disk));

  uint8_t *boot = s_disk[0];
  boot[0] = 0xEBu;
  boot[1] = 0x3Cu;
  boot[2] = 0x90u;
  memcpy(&boot[3], "MSDOS5.0", 8u);
  store_le16(&boot[11], MEMDISK_SECTOR_SIZE);
  boot[13] = 1u;             /* sectors per cluster */
  store_le16(&boot[14], 1u); /* reserved sectors */
  boot[16] = 1u;             /* FAT count */
  store_le16(&boot[17], MEMDISK_ROOT_ENTRIES);
  store_le16(&boot[19], MEMDISK_SECTOR_COUNT);
  boot[21] = 0xF8u; /* fixed disk media */
  store_le16(&boot[22], MEMDISK_FAT_SECTORS);
  store_le16(&boot[24], 63u);  /* sectors per track */
  store_le16(&boot[26], 255u); /* heads */
  boot[36] = 0x80u;            /* drive number */
  boot[38] = 0x29u;            /* extended boot sig */
  store_le32(&boot[39], 0x12345678u);
  memcpy(&boot[43], "JH MEMDISK ", 11u);
  memcpy(&boot[54], "FAT16   ", 8u);
  boot[510] = 0x55u;
  boot[511] = 0xAAu;

  uint8_t *fat = s_disk[1];
  fat[0] = 0xF8u;
  fat[1] = 0xFFu;
  fat[2] = 0xFFu;
  fat[3] = 0xFFu;

  s_initialized = false;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv) {
  if (pdrv != 0u) {
    return STA_NOINIT;
  }
  s_initialized = true;
  return 0u;
}

extern "C" DSTATUS disk_status(BYTE pdrv) {
  if (pdrv != 0u || !s_initialized) {
    return STA_NOINIT;
  }
  return 0u;
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
  if (pdrv != 0u || buff == NULL || count == 0u ||
      sector >= MEMDISK_SECTOR_COUNT ||
      (LBA_t)count > (MEMDISK_SECTOR_COUNT - sector)) {
    return RES_PARERR;
  }
  memcpy(buff, &s_disk[sector][0], (size_t)count * MEMDISK_SECTOR_SIZE);
  return RES_OK;
}

extern "C" DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector,
                              UINT count) {
  if (pdrv != 0u || buff == NULL || count == 0u ||
      sector >= MEMDISK_SECTOR_COUNT ||
      (LBA_t)count > (MEMDISK_SECTOR_COUNT - sector)) {
    return RES_PARERR;
  }
  memcpy(&s_disk[sector][0], buff, (size_t)count * MEMDISK_SECTOR_SIZE);
  return RES_OK;
}

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  if (pdrv != 0u) {
    return RES_PARERR;
  }

  switch (cmd) {
  case CTRL_SYNC:
    return RES_OK;
  case GET_SECTOR_COUNT:
    if (buff == NULL) {
      return RES_PARERR;
    }
    *(LBA_t *)buff = MEMDISK_SECTOR_COUNT;
    return RES_OK;
  case GET_SECTOR_SIZE:
    if (buff == NULL) {
      return RES_PARERR;
    }
    *(WORD *)buff = MEMDISK_SECTOR_SIZE;
    return RES_OK;
  case GET_BLOCK_SIZE:
    if (buff == NULL) {
      return RES_PARERR;
    }
    *(DWORD *)buff = 1u;
    return RES_OK;
  default:
    return RES_PARERR;
  }
}

extern "C" DWORD get_fattime(void) {
  return ((DWORD)(2026u - 1980u) << 25) | ((DWORD)6u << 21) |
         ((DWORD)24u << 16) | ((DWORD)12u << 11);
}

void setUp(void) {
  (void)f_mount(NULL, "0:", 0);
  memset(&s_fs, 0, sizeof(s_fs));
  init_fat16_image();
}

void tearDown(void) { (void)f_mount(NULL, "0:", 0); }

void test_ff16_mounts_fat16_ramdisk_and_roundtrips_file(void) {
  FIL file = {};
  const char payload[] = "JaszczurHAL FatFs memdisk\nline two\n";
  char readback[sizeof(payload)] = {};
  UINT transferred = 0u;

  TEST_ASSERT_EQUAL_INT(FR_OK, f_mount(&s_fs, "0:", 1));
  TEST_ASSERT_EQUAL_INT(
      FR_OK, f_open(&file, "0:HELLO.TXT", FA_CREATE_ALWAYS | FA_WRITE));
  TEST_ASSERT_EQUAL_INT(
      FR_OK, f_write(&file, payload, sizeof(payload) - 1u, &transferred));
  TEST_ASSERT_EQUAL_UINT(sizeof(payload) - 1u, transferred);
  TEST_ASSERT_EQUAL_INT(FR_OK, f_sync(&file));
  TEST_ASSERT_EQUAL_INT(FR_OK, f_close(&file));

  TEST_ASSERT_EQUAL_INT(FR_OK, f_open(&file, "0:HELLO.TXT", FA_READ));
  TEST_ASSERT_EQUAL_INT(
      FR_OK, f_read(&file, readback, sizeof(readback) - 1u, &transferred));
  TEST_ASSERT_EQUAL_UINT(sizeof(payload) - 1u, transferred);
  TEST_ASSERT_EQUAL_STRING(payload, readback);
  TEST_ASSERT_EQUAL_INT(FR_OK, f_close(&file));
}

void test_ff16_append_updates_existing_file(void) {
  FIL file = {};
  FILINFO info = {};
  char readback[16] = {};
  UINT transferred = 0u;

  TEST_ASSERT_EQUAL_INT(FR_OK, f_mount(&s_fs, "0:", 1));
  TEST_ASSERT_EQUAL_INT(
      FR_OK, f_open(&file, "0:APPEND.TXT", FA_CREATE_ALWAYS | FA_WRITE));
  TEST_ASSERT_EQUAL_INT(FR_OK, f_write(&file, "abc", 3u, &transferred));
  TEST_ASSERT_EQUAL_UINT(3u, transferred);
  TEST_ASSERT_EQUAL_INT(FR_OK, f_close(&file));

  TEST_ASSERT_EQUAL_INT(
      FR_OK, f_open(&file, "0:APPEND.TXT", FA_OPEN_APPEND | FA_WRITE));
  TEST_ASSERT_EQUAL_INT(FR_OK, f_write(&file, "def", 3u, &transferred));
  TEST_ASSERT_EQUAL_UINT(3u, transferred);
  TEST_ASSERT_EQUAL_INT(FR_OK, f_close(&file));

  TEST_ASSERT_EQUAL_INT(FR_OK, f_stat("0:APPEND.TXT", &info));
  TEST_ASSERT_EQUAL_UINT32(6u, info.fsize);
  TEST_ASSERT_EQUAL_INT(FR_OK, f_open(&file, "0:APPEND.TXT", FA_READ));
  TEST_ASSERT_EQUAL_INT(FR_OK, f_read(&file, readback, 6u, &transferred));
  TEST_ASSERT_EQUAL_UINT(6u, transferred);
  TEST_ASSERT_EQUAL_STRING("abcdef", readback);
  TEST_ASSERT_EQUAL_INT(FR_OK, f_close(&file));
}

void test_ff16_reports_no_filesystem_for_blank_media(void) {
  memset(s_disk, 0, sizeof(s_disk));

  TEST_ASSERT_EQUAL_INT(FR_OK, f_mount(&s_fs, "0:", 0));
  TEST_ASSERT_EQUAL_INT(FR_NO_FILESYSTEM, f_mount(&s_fs, "0:", 1));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ff16_mounts_fat16_ramdisk_and_roundtrips_file);
  RUN_TEST(test_ff16_append_updates_existing_file);
  RUN_TEST(test_ff16_reports_no_filesystem_for_blank_media);
  return UNITY_END();
}
