#include "jh_firmware_internal.h"
#include <inttypes.h>
#include <jb/filesystem.h>
#include <stdio.h>
#include <string.h>

static bool serial_valid(const char *serial) {
  if (serial == NULL || *serial == '\0' || strlen(serial) > 128)
    return false;
  for (const char *c = serial; *c; ++c)
    if (!jh_ascii_is_upper(*c) && !jh_ascii_is_lower(*c) &&
        !jh_ascii_is_digit(*c) && *c != '-' && *c != '_' && *c != '.' &&
        *c != ':')
      return false;
  return true;
}

static hal_status_t begin_output(const char *output, uint32_t size,
                                 jb_file_writer_t **writer,
                                 jh_failure_t *failure) {
  bool absolute = false;
  hal_status_t status = jb_fs_path_is_absolute(output, &absolute);
  jb_string_t path = {0};
  if (status == HAL_OK && !absolute) {
    status = jb_string_init(&path, jb_heap_allocator());
    if (status == HAL_OK)
      status = jh_working_directory(&path, failure);
#ifdef _WIN32
    if (status == HAL_OK)
      status = jh_absolute_path(output, jb_string_data(&path), &path, failure);
#else
    jh_put(&path, &status, "/");
    jh_put(&path, &status, output);
#endif
    output = jb_string_data(&path);
  }
  if (status == HAL_OK)
    status = jb_file_writer_begin(output, size, writer);
  jb_string_destroy(&path);
  return status;
}

hal_status_t jh_firmware_read(const jh_firmware_options_t *o,
                              jh_firmware_result_t *r,
                              jh_json_location_t *location,
                              jh_failure_t *failure) {
  if (r == NULL || failure == NULL || location == NULL)
    return HAL_EINVAL;
  memset(r, 0, sizeof *r);
  r->device_state = "unchanged";
  if (o == NULL || o->project == NULL || !*o->project || o->assembly == NULL ||
      !*o->assembly || o->output == NULL || !*o->output)
    return jh_fail_arguments(failure);
  jh_firmware_plan_t p = {0};
  p.options = o;
  p.failure = failure;
  p.location = location;
  jb_temp_dir_t *staging = NULL;
  jb_file_writer_t *writer = NULL;
  const char *temporary = NULL;
  bool complete_read = false, staged_file = false;
  jb_file_data_t bytes = {0};
  hal_status_t status = jh_list_init(&p.strings);
  if (status == HAL_OK)
    status = jh_firmware_prepare(&p);
  if (status != HAL_OK)
    goto done;
  r->target = p.target;
  r->address = p.address;
  r->size = p.size;
  r->flash_address = (uint32_t)p.regions[0].begin;
  r->flash_size = (uint32_t)(p.regions[0].end - p.regions[0].begin);
  r->complete = r->address == r->flash_address && r->size == r->flash_size;
  const bool esp = strcmp(p.target, "esp32s3") == 0;
  const bool stm = strcmp(p.target, "stm32g474") == 0;
  if (!esp && o->baud_rate) {
    status =
        jh_fail_text(failure, HAL_EINVAL, 2, "--baud requires an ESP build");
    goto done;
  }
  r->baud_rate = esp ? (o->baud_rate ? o->baud_rate : 115200u) : 0;
  r->timeout_ms = o->timeout_ms;
  if (o->automatic_timeout) {
    uint64_t budget = 120000;
    if (esp) {
      const uint64_t serial_ms =
          ((uint64_t)p.size * 20000 + r->baud_rate - 1) / r->baud_rate + 60000;
      if (serial_ms > budget)
        budget = serial_ms;
    }
    r->timeout_ms = budget > UINT32_MAX ? UINT32_MAX : (uint32_t)budget;
  }
  if ((esp && (o->port == NULL || !*o->port || o->port[0] == '-' ||
               o->serial != NULL)) ||
      (!esp && (!serial_valid(o->serial) || o->port != NULL))) {
    status = jh_fail_text(
        failure, HAL_EINVAL, 2,
        "ESP requires --port only; RP/STM require an ASCII --serial only");
    goto done;
  }
  if (stm && (!*p.interface_file || !*p.target_file)) {
    status =
        jh_fail_text(failure, HAL_ECONFIG, 2,
                     "build has no OpenOCD interface/target configuration");
    goto done;
  }
  /* Opening a sibling writer checks actual directory access and creates
   * missing parents without replacing the destination. */
  status = begin_output(o->output, p.size, &writer, failure);
  if (status != HAL_OK) {
    (void)jh_fail(
        failure, status, 3,
        JH_PARTS(jh_span(o->output), jh_span(": cannot prepare output file")));
    goto done;
  }
  if (o->canceled && o->canceled(o->context)) {
    status = HAL_ECANCELED;
    goto done;
  }
  status = jb_temp_dir_create(p.directory, &staging);
  if (status != HAL_OK)
    goto done;
  /* The owned directory reserves a unique name and isolates tool state.
   * Keep the dump beside it, so cleanup never discards a completed backup. */
  char name[128], file_argument[160];
  size_t required = 0;
  status = jb_fs_path_filename(jb_temp_dir_path(staging), name, sizeof name - 4,
                               &required);
  if (status != HAL_OK)
    goto done;
  if (!serial_valid(name)) {
    status = HAL_ECONFIG;
    goto done;
  }
  memcpy(name + required - 1, ".bin", 5);
  (void)snprintf(file_argument, sizeof file_argument, "../%s", name);
  temporary = jh_firmware_path(&p, p.directory, name);
  if (failure->status != HAL_OK) {
    status = failure->status;
    goto done;
  }
  jb_fs_info_t existing;
  status = jb_fs_status(temporary, false, &existing);
  if (status != HAL_ENOENT) {
    if (status == HAL_OK)
      status = HAL_EEXIST;
    goto done;
  }
  const char *private_file =
      jh_firmware_path(&p, jb_temp_dir_path(staging), "firmware.bin");
  if (failure->status != HAL_OK) {
    status = failure->status;
    goto done;
  }
  /* A same-filesystem move preserves the private inherited Windows DACL.
   * Opening the dump in the reader truncates this file, retaining access bits.
   */
  bool created = false;
  status =
      jb_file_write_atomic_mode(private_file, NULL, 0, false, 0600, &created);
  if (status == HAL_OK) {
    status = jb_fs_rename(private_file, temporary);
    staged_file = status == HAL_OK;
  }
  if (status != HAL_OK)
    goto done;
  char address[24], size[24], end[24], baud[16], adapter[160], commands[384];
  (void)snprintf(address, sizeof address, "0x%" PRIx32, p.address);
  (void)snprintf(size, sizeof size, "0x%" PRIx32, p.size);
  (void)snprintf(end, sizeof end, "0x%" PRIx64, (uint64_t)p.address + p.size);
  const char *arguments[24] = {0};
  size_t n = 0;
  const char *final_state;
  if (esp) {
    arguments[n++] = "-m";
    arguments[n++] = "esptool";
    arguments[n++] = "--chip";
    arguments[n++] = "esp32s3";
    arguments[n++] = "--port";
    arguments[n++] = o->port;
    (void)snprintf(baud, sizeof baud, "%" PRIu32, r->baud_rate);
    arguments[n++] = "--baud";
    arguments[n++] = baud;
    arguments[n++] = "--before";
    arguments[n++] = o->reset ? "default-reset" : "no-reset";
    arguments[n++] = "--after";
    arguments[n++] = o->reset ? "hard-reset" : "no-reset";
    arguments[n++] = "read-flash";
    arguments[n++] = address;
    arguments[n++] = r->complete ? "ALL" : size;
    arguments[n++] = file_argument;
    final_state = o->reset ? "running" : "bootloader";
  } else if (stm) {
    (void)snprintf(adapter, sizeof adapter, "adapter serial %s", o->serial);
    if (r->complete)
      (void)snprintf(commands, sizeof commands,
                     "init; %s; flash read_bank 0 %s; resume; shutdown",
                     o->reset ? "reset halt" : "halt", file_argument);
    else
      (void)snprintf(commands, sizeof commands,
                     "init; %s; dump_image %s %s %s; resume; shutdown",
                     o->reset ? "reset halt" : "halt", file_argument, address,
                     size);
    arguments[n++] = "-c";
    arguments[n++] =
        "gdb_port disabled; tcl_port disabled; telnet_port disabled";
    arguments[n++] = "-f";
    arguments[n++] = p.interface_file;
    arguments[n++] = "-f";
    arguments[n++] = p.target_file;
    arguments[n++] = "-c";
    arguments[n++] = adapter;
    arguments[n++] = "-c";
    arguments[n++] = commands;
    final_state = "running";
  } else {
    arguments[n++] = "save";
    arguments[n++] = "-r";
    arguments[n++] = address;
    arguments[n++] = end;
    arguments[n++] = file_argument;
    arguments[n++] = "-t";
    arguments[n++] = "bin";
    arguments[n++] = "--ser";
    arguments[n++] = o->serial;
    if (o->reset)
      arguments[n++] = "-F";
    final_state = "bootsel";
  }
  arguments[n] = NULL;
  const jb_process_options_t process = {
      p.tool,      arguments,     jb_temp_dir_path(staging),
      NULL,        r->timeout_ms, o->log,
      o->canceled, o->context};
  r->attempted = true;
  status = jb_process_run(&process, &r->process);
  if (r->process.started)
    r->device_state = "unknown";
  if (status == HAL_OK && (!r->process.exited || r->process.exit_code != 0 ||
                           r->process.term_signal != 0))
    status = HAL_EIO;
  if (status != HAL_OK) {
    (void)jh_fail_text(
        failure, status, status == HAL_ECANCELED ? 130 : 3,
        "firmware reader failed; see tool output and deviceState");
    goto done;
  }
  r->device_state = final_state;
  status = jb_file_read(temporary, p.size, &bytes);
  if (status == HAL_OK && bytes.length != p.size)
    status = HAL_EIO;
  if (status != HAL_OK) {
    (void)jh_fail_text(
        failure, status, 3,
        "firmware reader did not produce the requested byte count");
    goto done;
  }
  complete_read = true;
  jh_firmware_sha256(bytes.data, bytes.length, r->sha256);
  /* Remove private tool state before publishing. A cleanup error must not
   * report failure after silently replacing the caller's previous dump. */
  status = jb_temp_dir_remove(&staging);
  if (status != HAL_OK)
    goto done;
  if (o->canceled && o->canceled(o->context)) {
    status = HAL_ECANCELED;
    goto done;
  }
  status = jb_file_writer_write(writer, bytes.data, bytes.length);
  if (status == HAL_OK)
    status = jb_file_writer_commit(&writer, &r->published);
done: {
  const hal_status_t cleanup = jb_file_writer_abort(&writer);
  if (status == HAL_OK)
    status = cleanup;
}
  if (staging != NULL) {
    const hal_status_t cleanup = jb_temp_dir_remove(&staging);
    if (status == HAL_OK)
      status = cleanup;
  }
  if (staged_file && (!complete_read || status == HAL_OK)) {
    const hal_status_t cleanup = jb_fs_remove(temporary, false);
    if (status == HAL_OK)
      status = cleanup;
  }
  if (complete_read && status != HAL_OK)
    (void)jh_fail(
        failure, status, status == HAL_ECANCELED ? 130 : 3,
        JH_PARTS(
            jh_span(o->output),
            jh_span(": cannot complete output; complete dump retained at "),
            jh_span(temporary)));
  jb_file_release(&bytes);
  jh_list_destroy(&p.strings);
  if (failure->status == HAL_OK && status != HAL_OK)
    (void)jh_fail_text(failure, status, status == HAL_ECANCELED ? 130 : 3,
                       "cannot complete firmware read");
  return failure->status;
}
