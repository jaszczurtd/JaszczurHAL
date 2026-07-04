#pragma once

#include "hal_config.h"

#ifdef HAL_ENABLE_HTTP_FILES

#include "hal_http_server.h"
#include "hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file hal_http_files.h
 * @brief Small HTTP file-serving/upload adapter on top of hal_http_server.
 *
 * The module intentionally does not depend on a concrete filesystem API. An
 * application or backend supplies stat/read/write callbacks, so the same HTTP
 * layer can serve files from LittleFS, FatFs/SD, RAM, flash assets or tests.
 */

#ifndef HAL_HTTP_FILES_MAX_MOUNTS
#define HAL_HTTP_FILES_MAX_MOUNTS 2u
#endif

#ifndef HAL_HTTP_FILES_PATH_MAX
#define HAL_HTTP_FILES_PATH_MAX 128u
#endif

#ifndef HAL_HTTP_FILES_ETAG_MAX
#define HAL_HTTP_FILES_ETAG_MAX 48u
#endif

#ifndef HAL_HTTP_FILES_IO_BUFFER_SIZE
#define HAL_HTTP_FILES_IO_BUFFER_SIZE 128u
#endif

#ifndef HAL_HTTP_FILES_DEFAULT_INDEX
#define HAL_HTTP_FILES_DEFAULT_INDEX "index.html"
#endif

#if HAL_HTTP_FILES_MAX_MOUNTS < 1
#error "HAL_HTTP_FILES_MAX_MOUNTS must be at least 1"
#endif

#if HAL_HTTP_FILES_PATH_MAX < 8
#error "HAL_HTTP_FILES_PATH_MAX must be at least 8"
#endif

#if HAL_HTTP_FILES_ETAG_MAX < 16
#error "HAL_HTTP_FILES_ETAG_MAX must be at least 16"
#endif

#if HAL_HTTP_FILES_IO_BUFFER_SIZE < 16
#error "HAL_HTTP_FILES_IO_BUFFER_SIZE must be at least 16"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool exists;
  bool is_dir;
  size_t size;
  uint32_t mtime;
  const char *content_type;
  const char *etag;
} hal_http_file_info_t;

typedef hal_status_t (*hal_http_file_stat_cb_t)(const char *path,
                                                hal_http_file_info_t *out_info,
                                                void *user);

typedef hal_status_t (*hal_http_file_read_cb_t)(const char *path, size_t offset,
                                                void *buffer, size_t max_len,
                                                size_t *out_len, void *user);

typedef hal_status_t (*hal_http_file_write_cb_t)(const char *path,
                                                 size_t offset,
                                                 const void *data, size_t len,
                                                 bool final, void *user);

typedef struct {
  const char *url_prefix;
  const char *fs_root;
  const char *index_name;
  const char *upload_path;
  bool enable_upload;
  hal_http_file_stat_cb_t stat;
  hal_http_file_read_cb_t read;
  hal_http_file_write_cb_t write;
  void *user;
} hal_http_files_config_t;

hal_status_t hal_http_files_mount(const hal_http_files_config_t *config);

void hal_http_files_clear(void);

const char *hal_http_files_content_type_for_path(const char *path);

hal_status_t hal_http_files_make_etag(const char *path,
                                      const hal_http_file_info_t *info,
                                      char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_HTTP_FILES */
