#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_FAT

#include "hal_sd_file.h"

#include "../ff16/hal_sd_spi_diskio.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/system/hal_sync.h"

#include <string.h>

static FATFS s_sd_fs;
static bool s_sd_mounted = false;
static hal_mutex_t s_sd_mutex = NULL;

static void sd_file_ensure_mutex(void) {
  (void)jh_hal_mutex_create_once(&s_sd_mutex);
}

static BYTE sd_file_mode_flags(hal_sd_file_mode_t mode) {
  switch (mode) {
  case HAL_SD_FILE_READ:
    return FA_READ | FA_OPEN_EXISTING;
  case HAL_SD_FILE_WRITE_TRUNCATE:
    return FA_WRITE | FA_CREATE_ALWAYS;
  case HAL_SD_FILE_WRITE_APPEND:
    return FA_WRITE | FA_OPEN_APPEND;
  case HAL_SD_FILE_READ_WRITE:
    return FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
  default:
    return FA_READ | FA_OPEN_EXISTING;
  }
}

bool hal_sd_file_begin(uint8_t spi_bus, uint8_t cs_pin) {
  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);

  if (!s_sd_mounted) {
    hal_sd_spi_diskio_configure_pins(spi_bus, cs_pin);
    s_sd_mounted = (f_mount(&s_sd_fs, "", 1) == FR_OK);
  }

  const bool mounted = s_sd_mounted;
  hal_mutex_unlock(s_sd_mutex);
  return mounted;
}

void hal_sd_file_end(void) {
  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);

  if (s_sd_mounted) {
    (void)f_mount(NULL, "", 0);
    hal_sd_spi_diskio_reset();
    s_sd_mounted = false;
  }

  hal_mutex_unlock(s_sd_mutex);
}

bool hal_sd_file_is_mounted(void) {
  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);
  const bool mounted = s_sd_mounted;
  hal_mutex_unlock(s_sd_mutex);
  return mounted;
}

bool hal_sd_file_open(hal_sd_file_t *file, const char *path,
                      hal_sd_file_mode_t mode) {
  if (file == NULL || path == NULL || path[0] == '\0') {
    return false;
  }

  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);

  file->open = false;
  const bool ok = s_sd_mounted &&
                  (f_open(&file->fil, path, sd_file_mode_flags(mode)) == FR_OK);
  file->open = ok;

  hal_mutex_unlock(s_sd_mutex);
  return ok;
}

size_t hal_sd_file_write(hal_sd_file_t *file, const void *data, size_t len) {
  if (file == NULL || !file->open || data == NULL || len == 0u) {
    return 0u;
  }

  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);

  UINT written = 0u;
  const FRESULT res = f_write(&file->fil, data, (UINT)len, &written);

  hal_mutex_unlock(s_sd_mutex);
  return (res == FR_OK) ? (size_t)written : 0u;
}

size_t hal_sd_file_read(hal_sd_file_t *file, void *data, size_t len) {
  if (file == NULL || !file->open || data == NULL || len == 0u) {
    return 0u;
  }

  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);

  UINT read = 0u;
  const FRESULT res = f_read(&file->fil, data, (UINT)len, &read);

  hal_mutex_unlock(s_sd_mutex);
  return (res == FR_OK) ? (size_t)read : 0u;
}

size_t hal_sd_file_print(hal_sd_file_t *file, const char *text) {
  const char *s = (text != NULL) ? text : "";
  return hal_sd_file_write(file, s, strlen(s));
}

size_t hal_sd_file_println(hal_sd_file_t *file, const char *text) {
  size_t written = hal_sd_file_print(file, text);
  written += hal_sd_file_write(file, "\n", 1u);
  return written;
}

size_t hal_sd_file_size(const hal_sd_file_t *file) {
  if (file == NULL || !file->open) {
    return 0u;
  }

  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);
  const size_t size = (size_t)f_size(&file->fil);
  hal_mutex_unlock(s_sd_mutex);
  return size;
}

bool hal_sd_file_seek(hal_sd_file_t *file, size_t offset) {
  if (file == NULL || !file->open) {
    return false;
  }

  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);
  const bool ok = (f_lseek(&file->fil, (FSIZE_t)offset) == FR_OK);
  hal_mutex_unlock(s_sd_mutex);
  return ok;
}

bool hal_sd_file_flush(hal_sd_file_t *file) {
  if (file == NULL || !file->open) {
    return false;
  }

  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);
  const bool ok = (f_sync(&file->fil) == FR_OK);
  hal_mutex_unlock(s_sd_mutex);
  return ok;
}

bool hal_sd_file_close(hal_sd_file_t *file) {
  if (file == NULL || !file->open) {
    return false;
  }

  sd_file_ensure_mutex();
  hal_mutex_lock(s_sd_mutex);
  const bool ok = (f_close(&file->fil) == FR_OK);
  file->open = false;
  hal_mutex_unlock(s_sd_mutex);
  return ok;
}

bool hal_sd_file_is_open(const hal_sd_file_t *file) {
  return file != NULL && file->open;
}

#endif /* HAL_ENABLE_FAT */
