/** @file Target-neutral HTTP file service. */
#include "hal/hal_http_files.h"

#ifdef HAL_ENABLE_HTTP_FILES

#include <stdio.h>
#include <string.h>

#define HTTP_FILES_BOUNDARY_MAX 72u

typedef struct {
  bool used;
  char url_prefix[HAL_HTTP_FILES_PATH_MAX];
  char fs_root[HAL_HTTP_FILES_PATH_MAX];
  char index_name[HAL_HTTP_FILES_PATH_MAX];
  char upload_path[HAL_HTTP_FILES_PATH_MAX];
  bool enable_upload;
  hal_http_file_stat_cb_t stat;
  hal_http_file_read_cb_t read;
  hal_http_file_write_cb_t write;
  hal_http_file_authorize_cb_t authorize_upload;
  void *user;
} http_files_mount_t;

static http_files_mount_t s_mounts[HAL_HTTP_FILES_MAX_MOUNTS];

static char lower_ascii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool str_ieq(const char *a, const char *b) {
  if (!a || !b) {
    return false;
  }
  while (*a && *b) {
    if (lower_ascii(*a) != lower_ascii(*b)) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

static bool str_nieq(const char *a, const char *b, size_t len) {
  if (!a || !b) {
    return false;
  }
  for (size_t i = 0u; i < len; ++i) {
    if (a[i] == '\0' || b[i] == '\0') {
      return false;
    }
    if (lower_ascii(a[i]) != lower_ascii(b[i])) {
      return false;
    }
  }
  return true;
}

static bool copy_string(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0u || !src) {
    return false;
  }
  size_t len = strlen(src);
  if (len >= dst_size) {
    return false;
  }
  memcpy(dst, src, len + 1u);
  return true;
}

static bool append_string(char *dst, size_t dst_size, const char *src) {
  size_t len = strlen(dst);
  size_t add = strlen(src);
  if (len + add >= dst_size) {
    return false;
  }
  memcpy(dst + len, src, add + 1u);
  return true;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static hal_status_t url_decode_path(const char *in, char *out,
                                    size_t out_size) {
  if (!in || !out || out_size == 0u) {
    return HAL_EINVAL;
  }
  size_t pos = 0u;
  for (size_t i = 0u; in[i] != '\0'; ++i) {
    char c = in[i];
    if (c == '%') {
      int hi = hex_value(in[i + 1u]);
      int lo = hex_value(in[i + 2u]);
      if (hi < 0 || lo < 0) {
        return HAL_EPROTO;
      }
      c = (char)((hi << 4) | lo);
      i += 2u;
    } else if (c == '+') {
      c = ' ';
    }
    if (c == '\0' || c == '\\') {
      return HAL_EINVAL;
    }
    if (pos + 1u >= out_size) {
      return HAL_EOVERFLOW;
    }
    out[pos++] = c;
  }
  out[pos] = '\0';
  return HAL_OK;
}

static bool path_is_safe_relative(const char *path) {
  if (!path) {
    return false;
  }

  const char *p = path;
  while (*p == '/') {
    ++p;
  }

  while (*p) {
    const char *seg = p;
    while (*p && *p != '/') {
      ++p;
    }
    size_t len = (size_t)(p - seg);
    if ((len == 1u && seg[0] == '.') ||
        (len == 2u && seg[0] == '.' && seg[1] == '.')) {
      return false;
    }
    while (*p == '/') {
      ++p;
    }
  }
  return true;
}

static const char *basename_only(const char *path) {
  const char *base = path;
  for (const char *p = path; p && *p; ++p) {
    if (*p == '/' || *p == '\\') {
      base = p + 1;
    }
  }
  return base;
}

static bool normalize_prefix(char *dst, size_t dst_size, const char *src) {
  if (!src || src[0] != '/') {
    return false;
  }
  if (!copy_string(dst, dst_size, src)) {
    return false;
  }
  size_t len = strlen(dst);
  while (len > 1u && dst[len - 1u] == '/') {
    dst[--len] = '\0';
  }
  return true;
}

static bool build_fs_path(const http_files_mount_t *mount, const char *relative,
                          char *out, size_t out_size) {
  if (!mount || !relative || !out || out_size == 0u) {
    return false;
  }

  const char *rel = relative;
  while (*rel == '/') {
    ++rel;
  }
  if (!path_is_safe_relative(rel)) {
    return false;
  }

  if (!copy_string(out, out_size, mount->fs_root)) {
    return false;
  }
  size_t root_len = strlen(out);
  if (root_len == 0u) {
    if (!copy_string(out, out_size, "/")) {
      return false;
    }
    root_len = 1u;
  }
  if (out[root_len - 1u] != '/') {
    if (!append_string(out, out_size, "/")) {
      return false;
    }
  }
  return append_string(out, out_size, rel);
}

static const char *extension_of(const char *path) {
  const char *ext = NULL;
  for (const char *p = path; p && *p; ++p) {
    if (*p == '.') {
      ext = p + 1;
    } else if (*p == '/') {
      ext = NULL;
    }
  }
  return ext ? ext : "";
}

const char *hal_http_files_content_type_for_path(const char *path) {
  const char *ext = extension_of(path);
  if (str_ieq(ext, "html") || str_ieq(ext, "htm")) {
    return "text/html; charset=utf-8";
  }
  if (str_ieq(ext, "css")) {
    return "text/css; charset=utf-8";
  }
  if (str_ieq(ext, "js")) {
    return "application/javascript";
  }
  if (str_ieq(ext, "json")) {
    return "application/json";
  }
  if (str_ieq(ext, "txt") || str_ieq(ext, "log")) {
    return "text/plain; charset=utf-8";
  }
  if (str_ieq(ext, "png")) {
    return "image/png";
  }
  if (str_ieq(ext, "jpg") || str_ieq(ext, "jpeg")) {
    return "image/jpeg";
  }
  if (str_ieq(ext, "svg")) {
    return "image/svg+xml";
  }
  if (str_ieq(ext, "ico")) {
    return "image/x-icon";
  }
  if (str_ieq(ext, "wasm")) {
    return "application/wasm";
  }
  return "application/octet-stream";
}

hal_status_t hal_http_files_make_etag(const char *path,
                                      const hal_http_file_info_t *info,
                                      char *out, size_t out_size) {
  if (!path || !info || !out || out_size == 0u) {
    return HAL_EINVAL;
  }

  uint32_t hash = 2166136261u;
  for (const char *p = path; *p; ++p) {
    hash ^= (uint8_t)*p;
    hash *= 16777619u;
  }
  hash ^= (uint32_t)info->size;
  hash *= 16777619u;
  hash ^= info->mtime;

  int written = snprintf(out, out_size, "W/\"%08lx-%08lx\"",
                         (unsigned long)hash, (unsigned long)info->size);
  if (written < 0 || (size_t)written >= out_size) {
    return HAL_EOVERFLOW;
  }
  return HAL_OK;
}

static hal_status_t write_status_text(hal_http_response_t *response,
                                      uint16_t code, const char *reason,
                                      const char *text) {
  hal_status_t status = hal_http_response_set_status(response, code, reason);
  if (status != HAL_OK) {
    return status;
  }
  status = hal_http_response_set_content_type(response, "text/plain");
  if (status != HAL_OK) {
    return status;
  }
  return hal_http_response_write_str(response, text);
}

static hal_status_t map_url_to_relative(const http_files_mount_t *mount,
                                        const char *url_path, char *relative,
                                        size_t relative_size) {
  size_t prefix_len = strlen(mount->url_prefix);
  if (strncmp(url_path, mount->url_prefix, prefix_len) != 0) {
    return HAL_ENOENT;
  }
  const char *suffix = url_path + prefix_len;
  while (*suffix == '/') {
    ++suffix;
  }

  hal_status_t status = url_decode_path(suffix, relative, relative_size);
  if (status != HAL_OK) {
    return status;
  }
  if (!path_is_safe_relative(relative)) {
    return HAL_EPERM;
  }
  if (relative[0] == '\0' || relative[strlen(relative) - 1u] == '/') {
    if (!append_string(relative, relative_size, mount->index_name)) {
      return HAL_EOVERFLOW;
    }
  }
  return HAL_OK;
}

static hal_status_t serve_file(const http_files_mount_t *mount,
                               const hal_http_request_t *request,
                               hal_http_response_t *response) {
  char relative[HAL_HTTP_FILES_PATH_MAX];
  hal_status_t status =
      map_url_to_relative(mount, request->path, relative, sizeof(relative));
  if (status != HAL_OK) {
    return write_status_text(response, 400u, "Bad Request", "Bad Request\n");
  }

  char fs_path[HAL_HTTP_FILES_PATH_MAX];
  if (!build_fs_path(mount, relative, fs_path, sizeof(fs_path))) {
    return write_status_text(response, 403u, "Forbidden", "Forbidden\n");
  }

  hal_http_file_info_t info = {};
  status = mount->stat(fs_path, &info, mount->user);
  if (status != HAL_OK || !info.exists || info.is_dir) {
    return write_status_text(response, 404u, "Not Found", "Not Found\n");
  }

  char etag[HAL_HTTP_FILES_ETAG_MAX];
  const char *etag_value = info.etag;
  if (!etag_value &&
      hal_http_files_make_etag(fs_path, &info, etag, sizeof(etag)) == HAL_OK) {
    etag_value = etag;
  }
  if (etag_value) {
    hal_http_response_set_header(response, "ETag", etag_value);
    const char *if_none_match =
        hal_http_request_get_header(request, "If-None-Match");
    if (if_none_match && strcmp(if_none_match, etag_value) == 0) {
      return hal_http_response_set_status(response, 304u, "Not Modified");
    }
  }

  status = hal_http_response_set_content_type(
      response, info.content_type
                    ? info.content_type
                    : hal_http_files_content_type_for_path(fs_path));
  if (status != HAL_OK) {
    return status;
  }
  hal_http_response_set_header(response, "Cache-Control", "no-cache");

  if (info.size >= HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE) {
    return write_status_text(response, 413u, "Payload Too Large",
                             "Payload Too Large\n");
  }

  uint8_t buffer[HAL_HTTP_FILES_IO_BUFFER_SIZE];
  size_t offset = 0u;
  while (true) {
    size_t read_len = 0u;
    status = mount->read(fs_path, offset, buffer, sizeof(buffer), &read_len,
                         mount->user);
    if (status != HAL_OK) {
      return write_status_text(response, 500u, "Internal Server Error",
                               "Read failed\n");
    }
    if (read_len == 0u) {
      break;
    }
    status = hal_http_response_write(response, buffer, read_len);
    if (status != HAL_OK) {
      return write_status_text(response, 413u, "Payload Too Large",
                               "Payload Too Large\n");
    }
    offset += read_len;
  }

  return HAL_OK;
}

static const uint8_t *find_bytes(const uint8_t *haystack, size_t haystack_len,
                                 const char *needle, size_t needle_len) {
  if (!haystack || !needle || needle_len == 0u || haystack_len < needle_len) {
    return NULL;
  }
  for (size_t i = 0u; i + needle_len <= haystack_len; ++i) {
    if (memcmp(haystack + i, needle, needle_len) == 0) {
      return haystack + i;
    }
  }
  return NULL;
}

static bool copy_header_value(const char *headers, size_t headers_len,
                              const char *name, char *out, size_t out_size) {
  if (!headers || !name || !out || out_size == 0u) {
    return false;
  }
  out[0] = '\0';
  size_t name_len = strlen(name);
  size_t pos = 0u;
  while (pos < headers_len) {
    size_t line_start = pos;
    while (pos + 1u < headers_len &&
           !(headers[pos] == '\r' && headers[pos + 1u] == '\n')) {
      ++pos;
    }
    if (pos + 1u >= headers_len) {
      pos = headers_len;
    }
    size_t line_len = pos - line_start;
    if (line_len > name_len && str_nieq(headers + line_start, name, name_len) &&
        headers[line_start + name_len] == ':') {
      size_t value_start = line_start + name_len + 1u;
      while (value_start < line_start + line_len &&
             (headers[value_start] == ' ' || headers[value_start] == '\t')) {
        ++value_start;
      }
      size_t value_end = line_start + line_len;
      while (value_end > value_start && (headers[value_end - 1u] == ' ' ||
                                         headers[value_end - 1u] == '\t')) {
        --value_end;
      }
      size_t value_len = value_end - value_start;
      if (value_len >= out_size) {
        return false;
      }
      memcpy(out, headers + value_start, value_len);
      out[value_len] = '\0';
      return true;
    }
    if (pos + 1u < headers_len && headers[pos] == '\r' &&
        headers[pos + 1u] == '\n') {
      pos += 2u;
    } else {
      break;
    }
  }
  return false;
}

static bool extract_quoted_param(const char *text, const char *name, char *out,
                                 size_t out_size) {
  if (!text || !name || !out || out_size == 0u) {
    return false;
  }
  out[0] = '\0';
  size_t name_len = strlen(name);
  const char *p = text;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == ';') {
      ++p;
    }
    if (str_nieq(p, name, name_len) && p[name_len] == '=') {
      p += name_len + 1u;
      if (*p == '"') {
        ++p;
        const char *end = strchr(p, '"');
        if (!end) {
          return false;
        }
        size_t len = (size_t)(end - p);
        if (len >= out_size) {
          return false;
        }
        memcpy(out, p, len);
        out[len] = '\0';
        return true;
      }
    }
    const char *semi = strchr(p, ';');
    if (!semi) {
      break;
    }
    p = semi + 1;
  }
  return false;
}

static bool extract_quoted_param_loose(const char *text, const char *name,
                                       char *out, size_t out_size) {
  if (!text || !name || !out || out_size == 0u) {
    return false;
  }

  char pattern[32];
  int written = snprintf(pattern, sizeof(pattern), "%s=\"", name);
  if (written < 0 || (size_t)written >= sizeof(pattern)) {
    return false;
  }

  const char *p = strstr(text, pattern);
  if (!p) {
    return false;
  }
  p += strlen(pattern);
  const char *end = strchr(p, '"');
  if (!end) {
    return false;
  }
  size_t len = (size_t)(end - p);
  if (len >= out_size) {
    return false;
  }
  memcpy(out, p, len);
  out[len] = '\0';
  return true;
}

static bool extract_boundary(const char *content_type, char *out,
                             size_t out_size) {
  if (!content_type || !out || out_size == 0u) {
    return false;
  }
  const char *p = strstr(content_type, "boundary=");
  if (!p) {
    return false;
  }
  p += 9u;
  if (*p == '"') {
    ++p;
    const char *end = strchr(p, '"');
    if (!end) {
      return false;
    }
    size_t len = (size_t)(end - p);
    if (len == 0u || len >= out_size) {
      return false;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
  }

  size_t len = 0u;
  while (p[len] && p[len] != ';' && p[len] != ' ' && p[len] != '\t' &&
         p[len] != '\r' && p[len] != '\n') {
    ++len;
  }
  if (len == 0u || len >= out_size) {
    return false;
  }
  memcpy(out, p, len);
  out[len] = '\0';
  return true;
}

static hal_status_t write_upload_file(const http_files_mount_t *mount,
                                      const char *directory,
                                      const char *filename, const void *data,
                                      size_t len) {
  const char *base = basename_only(filename);
  if (!base || base[0] == '\0') {
    return HAL_EINVAL;
  }

  char relative[HAL_HTTP_FILES_PATH_MAX];
  relative[0] = '\0';
  if (directory && directory[0]) {
    hal_status_t status =
        url_decode_path(directory, relative, sizeof(relative));
    if (status != HAL_OK) {
      return status;
    }
    if (!path_is_safe_relative(relative)) {
      return HAL_EPERM;
    }
    size_t len_dir = strlen(relative);
    if (len_dir > 0u && relative[len_dir - 1u] != '/') {
      if (!append_string(relative, sizeof(relative), "/")) {
        return HAL_EOVERFLOW;
      }
    }
  }
  if (!append_string(relative, sizeof(relative), base)) {
    return HAL_EOVERFLOW;
  }

  char fs_path[HAL_HTTP_FILES_PATH_MAX];
  if (!build_fs_path(mount, relative, fs_path, sizeof(fs_path))) {
    return HAL_EPERM;
  }
  return mount->write(fs_path, 0u, data, len, true, mount->user);
}

static hal_status_t handle_multipart_upload(const http_files_mount_t *mount,
                                            const hal_http_request_t *request,
                                            hal_http_response_t *response) {
  const char *content_type =
      hal_http_request_get_header(request, "Content-Type");
  char boundary[HTTP_FILES_BOUNDARY_MAX];
  if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
    return write_status_text(response, 400u, "Bad Request",
                             "Missing multipart boundary\n");
  }

  char marker[HTTP_FILES_BOUNDARY_MAX + 2u];
  marker[0] = '-';
  marker[1] = '-';
  if (!copy_string(marker + 2u, sizeof(marker) - 2u, boundary)) {
    return write_status_text(response, 400u, "Bad Request",
                             "Boundary too long\n");
  }
  size_t marker_len = strlen(marker);

  const uint8_t *body = (const uint8_t *)request->body;
  const uint8_t *end = body + request->body_len;
  const uint8_t *part = find_bytes(body, request->body_len, marker, marker_len);
  char upload_dir[HAL_HTTP_FILES_PATH_MAX] = "";
  bool wrote_file = false;

  while (part) {
    part += marker_len;
    if (part + 2u <= end && part[0] == '-' && part[1] == '-') {
      break;
    }
    if (part + 2u > end || part[0] != '\r' || part[1] != '\n') {
      return write_status_text(response, 400u, "Bad Request",
                               "Malformed multipart body\n");
    }
    part += 2u;

    const uint8_t *headers_end =
        find_bytes(part, (size_t)(end - part), "\r\n\r\n", 4u);
    if (!headers_end) {
      return write_status_text(response, 400u, "Bad Request",
                               "Malformed multipart headers\n");
    }
    const uint8_t *data = headers_end + 4u;
    const uint8_t *next = find_bytes(data, (size_t)(end - data), "\r\n--", 4u);
    if (!next) {
      return write_status_text(response, 400u, "Bad Request",
                               "Missing multipart terminator\n");
    }

    size_t headers_len = (size_t)(headers_end - part);
    size_t data_len = (size_t)(next - data);
    char disposition[160];
    char name[HAL_HTTP_FILES_PATH_MAX];
    char filename[HAL_HTTP_FILES_PATH_MAX];
    bool has_disposition = copy_header_value((const char *)part, headers_len,
                                             "Content-Disposition", disposition,
                                             sizeof(disposition));
    name[0] = '\0';
    filename[0] = '\0';
    if (has_disposition) {
      extract_quoted_param(disposition, "name", name, sizeof(name));
      extract_quoted_param(disposition, "filename", filename, sizeof(filename));
      if (name[0] == '\0') {
        extract_quoted_param_loose(disposition, "name", name, sizeof(name));
      }
      if (filename[0] == '\0') {
        extract_quoted_param_loose(disposition, "filename", filename,
                                   sizeof(filename));
      }
    }

    if (filename[0] != '\0') {
      hal_status_t status =
          write_upload_file(mount, upload_dir, filename, data, data_len);
      if (status != HAL_OK) {
        return write_status_text(response, 500u, "Internal Server Error",
                                 "Upload write failed\n");
      }
      wrote_file = true;
    } else if (strcmp(name, "path") == 0) {
      size_t copy_len = data_len;
      if (copy_len >= sizeof(upload_dir)) {
        return write_status_text(response, 413u, "Payload Too Large",
                                 "Upload path too long\n");
      }
      memcpy(upload_dir, data, copy_len);
      upload_dir[copy_len] = '\0';
    }

    part = next + 2u;
    if ((size_t)(end - part) < marker_len) {
      break;
    }
    if (memcmp(part, marker, marker_len) != 0) {
      part = find_bytes(part, (size_t)(end - part), marker, marker_len);
    }
  }

  if (!wrote_file) {
    return write_status_text(response, 400u, "Bad Request", "No file part\n");
  }
  hal_http_response_set_status(response, 201u, "Created");
  return hal_http_response_write_str(response, "Uploaded\n");
}

static hal_status_t handle_raw_upload(const http_files_mount_t *mount,
                                      const hal_http_request_t *request,
                                      hal_http_response_t *response) {
  char relative[HAL_HTTP_FILES_PATH_MAX];
  hal_status_t status =
      map_url_to_relative(mount, request->path, relative, sizeof(relative));
  if (status != HAL_OK) {
    return write_status_text(response, 400u, "Bad Request", "Bad Request\n");
  }

  char fs_path[HAL_HTTP_FILES_PATH_MAX];
  if (!build_fs_path(mount, relative, fs_path, sizeof(fs_path))) {
    return write_status_text(response, 403u, "Forbidden", "Forbidden\n");
  }

  status = mount->write(fs_path, 0u, request->body, request->body_len, true,
                        mount->user);
  if (status != HAL_OK) {
    return write_status_text(response, 500u, "Internal Server Error",
                             "Upload write failed\n");
  }
  hal_http_response_set_status(response, 201u, "Created");
  return hal_http_response_write_str(response, "Uploaded\n");
}

static hal_status_t file_route_handler(const hal_http_request_t *request,
                                       hal_http_response_t *response,
                                       void *user) {
  http_files_mount_t *mount = (http_files_mount_t *)user;
  if (!mount || !mount->used) {
    return write_status_text(response, 404u, "Not Found", "Not Found\n");
  }

  if (request->method == HAL_HTTP_METHOD_GET ||
      request->method == HAL_HTTP_METHOD_HEAD) {
    return serve_file(mount, request, response);
  }

  if (request->method == HAL_HTTP_METHOD_PUT && mount->enable_upload &&
      mount->write) {
    if (mount->authorize_upload(request, HAL_HTTP_FILE_UPLOAD_RAW,
                                mount->user) != HAL_OK) {
      return write_status_text(response, 403u, "Forbidden", "Forbidden\n");
    }
    return handle_raw_upload(mount, request, response);
  }

  if (request->method == HAL_HTTP_METHOD_POST && mount->enable_upload &&
      mount->write) {
    if (mount->authorize_upload(request, HAL_HTTP_FILE_UPLOAD_MULTIPART,
                                mount->user) != HAL_OK) {
      return write_status_text(response, 403u, "Forbidden", "Forbidden\n");
    }
    return handle_multipart_upload(mount, request, response);
  }

  return write_status_text(response, 405u, "Method Not Allowed",
                           "Method Not Allowed\n");
}

hal_status_t hal_http_files_mount(const hal_http_files_config_t *config) {
  if (!config || !config->url_prefix || config->url_prefix[0] != '/' ||
      !config->stat || !config->read) {
    return HAL_EINVAL;
  }
  if (config->enable_upload && (!config->write || !config->authorize_upload)) {
    return HAL_EINVAL;
  }

  http_files_mount_t *slot = NULL;
  for (size_t i = 0u; i < HAL_HTTP_FILES_MAX_MOUNTS; ++i) {
    if (!s_mounts[i].used) {
      slot = &s_mounts[i];
      break;
    }
  }
  if (!slot) {
    return HAL_ENOMEM;
  }

  memset(slot, 0, sizeof(*slot));
  if (!normalize_prefix(slot->url_prefix, sizeof(slot->url_prefix),
                        config->url_prefix)) {
    return HAL_EINVAL;
  }
  if (!copy_string(slot->fs_root, sizeof(slot->fs_root),
                   config->fs_root ? config->fs_root : "/")) {
    return HAL_EOVERFLOW;
  }
  if (!copy_string(slot->index_name, sizeof(slot->index_name),
                   config->index_name ? config->index_name
                                      : HAL_HTTP_FILES_DEFAULT_INDEX)) {
    return HAL_EOVERFLOW;
  }
  const char *upload_path =
      config->upload_path ? config->upload_path : "/upload";
  if (!normalize_prefix(slot->upload_path, sizeof(slot->upload_path),
                        upload_path)) {
    return HAL_EINVAL;
  }
  slot->enable_upload = config->enable_upload;
  slot->stat = config->stat;
  slot->read = config->read;
  slot->write = config->write;
  slot->authorize_upload = config->authorize_upload;
  slot->user = config->user;
  slot->used = true;

  hal_status_t status = hal_http_server_route_prefix(
      HAL_HTTP_METHOD_GET, slot->url_prefix, file_route_handler, slot);
  if (status == HAL_OK) {
    status = hal_http_server_route_prefix(
        HAL_HTTP_METHOD_HEAD, slot->url_prefix, file_route_handler, slot);
  }
  if (status == HAL_OK && slot->enable_upload) {
    status = hal_http_server_route_prefix(HAL_HTTP_METHOD_PUT, slot->url_prefix,
                                          file_route_handler, slot);
  }
  if (status == HAL_OK && slot->enable_upload) {
    status = hal_http_server_route(HAL_HTTP_METHOD_POST, slot->upload_path,
                                   file_route_handler, slot);
  }
  if (status != HAL_OK) {
    memset(slot, 0, sizeof(*slot));
  }
  return status;
}

void hal_http_files_clear(void) { memset(s_mounts, 0, sizeof(s_mounts)); }

#endif /* HAL_ENABLE_HTTP_FILES */
