#pragma once

#include "../ff16/ff.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HAL_SD_FILE_READ = 0,
  HAL_SD_FILE_WRITE_TRUNCATE,
  HAL_SD_FILE_WRITE_APPEND,
  HAL_SD_FILE_READ_WRITE,
} hal_sd_file_mode_t;

typedef struct {
  FIL fil;
  bool open;
} hal_sd_file_t;

bool hal_sd_file_begin(uint8_t spi_bus, uint8_t cs_pin);
void hal_sd_file_end(void);
bool hal_sd_file_is_mounted(void);

bool hal_sd_file_open(hal_sd_file_t *file, const char *path,
                      hal_sd_file_mode_t mode);
size_t hal_sd_file_write(hal_sd_file_t *file, const void *data, size_t len);
size_t hal_sd_file_read(hal_sd_file_t *file, void *data, size_t len);
size_t hal_sd_file_print(hal_sd_file_t *file, const char *text);
size_t hal_sd_file_println(hal_sd_file_t *file, const char *text);
size_t hal_sd_file_size(const hal_sd_file_t *file);
bool hal_sd_file_seek(hal_sd_file_t *file, size_t offset);
bool hal_sd_file_flush(hal_sd_file_t *file);
bool hal_sd_file_close(hal_sd_file_t *file);
bool hal_sd_file_is_open(const hal_sd_file_t *file);

#ifdef __cplusplus
}
#endif
