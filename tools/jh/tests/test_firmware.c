#include "hal/core/jh_endian.h"
#include "jh_firmware_read.h"
#include <jb/filesystem.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <aclapi.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static unsigned failures;
static jb_string_t last_error;
static void expect(bool condition, const char *message) {
  if (!condition) {
    (void)fprintf(stderr, "firmware: %s\n", message);
    ++failures;
  }
}

/* A deliberately strict substitute for the three external readers. Independent
 * addresses and argument spellings catch wrong target/range/device selection.
 */
static int reader(int argc, char **argv) {
  const char *mode = "OK";
  const char *filename = "firmware.bin";
  char stm_filename[160] = {0}, stm_command[384];
  bool partial = false;
  if (strcmp(argv[1], "save") == 0) {
    partial = argc > 4 && strcmp(argv[3], "0x12340080") == 0;
    if (argc > 5)
      filename = argv[5];
    if ((argc != 10 && argc != 11) || strcmp(argv[2], "-r") ||
        (!partial && strcmp(argv[3], "0x12340000")) ||
        strcmp(argv[4], "0x12340100") || strcmp(argv[6], "-t") ||
        strcmp(argv[7], "bin") || strcmp(argv[8], "--ser") ||
        (argc == 11 && strcmp(argv[10], "-F")))
      return 61;
    mode = argv[9];
  } else if (strcmp(argv[1], "-c") == 0) {
    static const char prefix[] = "init; halt; flash read_bank 0 ";
    if (argc == 11 && !strncmp(argv[10], prefix, strlen(prefix))) {
      const char *start = argv[10] + strlen(prefix);
      const char *end = strchr(start, ';');
      if (end && (size_t)(end - start) < sizeof stm_filename) {
        memcpy(stm_filename, start, (size_t)(end - start));
        filename = stm_filename;
      }
    }
    (void)snprintf(stm_command, sizeof stm_command,
                   "init; halt; flash read_bank 0 %s; resume; shutdown",
                   filename);
    if (argc != 11 ||
        strcmp(argv[2],
               "gdb_port disabled; tcl_port disabled; telnet_port disabled") ||
        strcmp(argv[3], "-f") || strcmp(argv[4], "interface/stlink.cfg") ||
        strcmp(argv[5], "-f") || strcmp(argv[6], "target/stm32g4x.cfg") ||
        strcmp(argv[7], "-c") || strcmp(argv[8], "adapter serial OK") ||
        strcmp(argv[9], "-c") || strcmp(argv[10], stm_command))
      return 62;
  } else if (strcmp(argv[1], "-m") == 0) {
    const int shift = argc == 17 ? 2 : 0;
    if (argc == 15 || argc == 17)
      filename = argv[14 + shift];
    if ((argc != 15 && argc != 17) || strcmp(argv[2], "esptool") ||
        strcmp(argv[3], "--chip") || strcmp(argv[4], "esp32s3") ||
        strcmp(argv[5], "--port") ||
        (strcmp(argv[6], "fixture-port") && strcmp(argv[6], "fast-port")) ||
        (shift &&
         (strcmp(argv[7], "--baud") ||
          strcmp(argv[8],
                 !strcmp(argv[6], "fast-port") ? "921600" : "115200"))) ||
        strcmp(argv[7 + shift], "--before") ||
        strcmp(argv[8 + shift], "default-reset") ||
        strcmp(argv[9 + shift], "--after") ||
        strcmp(argv[10 + shift], "hard-reset") ||
        strcmp(argv[11 + shift], "read-flash") ||
        strcmp(argv[12 + shift], "0x0") || strcmp(argv[13 + shift], "ALL"))
      return 63;
  } else {
    return 64;
  }
  if (strcmp(mode, "WAIT") == 0) {
    const time_t until = time(NULL) + 3;
    while (time(NULL) < until) {
    }
    return 65;
  }
  /* Old and corrected staging names, for running regressions on both. */
  if (strcmp(filename, "firmware.bin") && strncmp(filename, "../.jb-dir-", 11))
    return 70;
  FILE *file = fopen(filename, "wb");
  if (file == NULL)
    return 66;
#ifdef _WIN32
  PSECURITY_DESCRIPTOR descriptor = NULL;
  PACL access = NULL;
  const DWORD security = GetNamedSecurityInfoA(
      (char *)filename, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL,
      &access, NULL, &descriptor);
  const bool private_file =
      security == ERROR_SUCCESS && access && access->AceCount == 1;
  if (descriptor)
    (void)LocalFree(descriptor);
  if (!private_file) {
    (void)fclose(file);
    (void)fprintf(stderr, "reader staging does not have a private DACL\n");
    return 71;
  }
#else
  struct stat metadata;
  if (strcmp(filename, "firmware.bin") &&
      (stat(filename, &metadata) != 0 || (metadata.st_mode & 0777) != 0600)) {
    (void)fclose(file);
    return 71;
  }
#endif
  const unsigned length = partial                      ? 128u
                          : strcmp(argv[1], "-m") == 0 ? 1024u * 1024u
                          : strcmp(mode, "SHORT") == 0 ? 255u
                          : strcmp(mode, "LONG") == 0  ? 257u
                                                       : 256u;
  for (unsigned i = 0; i < length; ++i)
    if (fputc((int)(i & 255u), file) == EOF) {
      (void)fclose(file);
      return 67;
    }
  if (fclose(file) != 0)
    return 68;
  (void)puts("complete");
  return strcmp(mode, "FAIL") == 0 ? 69 : 0;
}

static const char *owned(jb_vector_t *strings, jb_json_span_t text) {
  expect(jh_list_push(strings, text) == HAL_OK, "store test path");
  return jb_string_data(jh_list_at(strings, jb_vector_size(strings) - 1));
}
static const char *path(jb_vector_t *strings, const char *base,
                        const char *name) {
  jb_string_t result = {0};
  (void)jb_string_init(&result, jb_heap_allocator());
  expect(jh_path_join(base, name, &result) == HAL_OK, "join path");
  const char *out = owned(strings, jh_string_span(&result));
  jb_string_destroy(&result);
  return out;
}
static void write_bytes(const char *file, const void *bytes, size_t length) {
  bool changed = false;
  expect(jb_file_write_atomic(file, bytes, length, false, &changed) == HAL_OK,
         "write fixture");
}
static void write_text(const char *file, const char *text) {
  write_bytes(file, text, strlen(text));
}
static void cache(jb_vector_t *strings, const char *build, const char *project,
                  const char *target, const char *tool) {
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  expect(jh_join(&text,
                 JH_PARTS(jh_span("JH_PROJECT_DIR:PATH="), jh_span(project),
                          jh_span("\nJH_ASSEMBLY:STRING=fixture\nJH_VARIANT:"
                                  "STRING=\nJH_TARGET:STRING="),
                          jh_span(target), jh_span("\nJH_ARTIFACT_DIR:PATH="),
                          jh_span(build),
                          jh_span("\nJH_PICOTOOL_EXECUTABLE:FILEPATH="),
                          jh_span(tool), jh_span("\nOPENOCD_BIN:FILEPATH="),
                          jh_span(tool),
                          jh_span("\nOPENOCD_INTERFACE:STRING=interface/"
                                  "stlink.cfg\nOPENOCD_TARGET:STRING=target/"
                                  "stm32g4x.cfg\n"))) == HAL_OK,
         "compose cache");
  write_bytes(path(strings, build, "CMakeCache.txt"), jb_string_data(&text),
              jb_string_size(&text));
  jb_string_destroy(&text);
}
static jh_firmware_result_t run(const jh_firmware_options_t *options,
                                hal_status_t expected) {
  jh_failure_t error;
  jh_json_location_t location;
  jh_failure_init(&error);
  jh_json_location_init(&location);
  jh_firmware_result_t result;
  const hal_status_t status =
      jh_firmware_read(options, &result, &location, &error);
  if (status != expected)
    (void)fprintf(stderr, "expected %s, got %s: %s\n",
                  hal_status_to_string(expected), hal_status_to_string(status),
                  jb_string_data(&error.message));
  expect(status == expected, "read status");
  expect(jb_string_assign(&last_error, jb_string_data(&error.message),
                          jb_string_size(&error.message)) == HAL_OK,
         "retain error message");
  jh_failure_destroy(&error);
  jh_json_location_destroy(&location);
  return result;
}
static void unchanged(const char *output) {
  jb_file_data_t data = {0};
  expect(jb_file_read(output, 1024, &data) == HAL_OK && data.length == 8 &&
             memcmp(data.data, "previous", 8) == 0,
         "failure preserves previous output");
  jb_file_release(&data);
}
static bool canceled(void *context) {
  unsigned *calls = context;
  return ++*calls > 1;
}
static bool cancel_before_read(void *context) {
  (void)context;
  return true;
}

static hal_status_t block_publication(void *context, jb_process_stream_t stream,
                                      const unsigned char *data,
                                      size_t length) {
  (void)stream;
  (void)data;
  if (length) {
    const char *output = context;
    expect(jb_fs_mkdir(output, false) == HAL_OK,
           "block destination after read");
  }
  return HAL_OK;
}

static void output_failures(jb_vector_t *strings,
                            const jh_firmware_options_t *initial) {
  jh_firmware_options_t options = *initial;
  const char *directory = path(strings, options.project, "destination");
  options.output = path(strings, directory, "backup.bin");
  /* Missing parents still work; a read-only parent must prevent tool launch. */
  (void)run(&options, HAL_OK);
  expect(jb_fs_remove(options.output, false) == HAL_OK,
         "remove destination fixture");
#ifndef _WIN32
  expect(chmod(directory, 0555) == 0, "make destination read-only");
  FILE *probe = fopen(options.output, "wb");
  if (probe == NULL) {
    const jh_firmware_result_t denied = run(&options, HAL_EPERM);
    expect(!denied.attempted && !strcmp(denied.device_state, "unchanged"),
           "unwritable destination rejected before touching hardware");
    expect(strstr(jb_string_data(&last_error), options.output) != NULL,
           "destination failure names output");
  } else {
    (void)fclose(probe);
    expect(jb_fs_remove(options.output, false) == HAL_OK,
           "remove privilege probe");
    (void)fprintf(
        stderr,
        "permission test skipped: filesystem permits writes despite 0555\n");
  }
  expect(chmod(directory, 0700) == 0, "restore destination access");
#endif
  options.log = block_publication;
  options.context = (void *)options.output;
  const jh_firmware_result_t failed = run(&options, HAL_ETYPE);
  expect(failed.attempted && !failed.published &&
             !strcmp(failed.device_state, "bootsel"),
         "publication failure keeps completed tool state");
  const char *marker = strstr(jb_string_data(&last_error), "retained at ");
  expect(marker != NULL && strstr(jb_string_data(&last_error), options.output),
         "publication failure names destination and recovery file");
  if (marker) {
    const char *saved = marker + strlen("retained at ");
    jb_file_data_t bytes = {0};
    expect(jb_file_read(saved, 256, &bytes) == HAL_OK && bytes.length == 256,
           "complete dump survives publication failure");
    for (size_t i = 0; i < bytes.length; ++i)
      expect(bytes.data[i] == (unsigned char)i, "retained dump bytes");
    jb_file_release(&bytes);
    expect(jb_fs_remove(saved, false) == HAL_OK,
           "remove recovered dump fixture");
  }
  expect(jb_fs_remove(directory, true) == HAL_OK, "remove output fixtures");
}

static hal_status_t cli_output(void *context, jb_process_stream_t stream,
                               const unsigned char *data, size_t length) {
  jb_string_t *text = context;
  if (stream != JB_PROCESS_STDOUT)
    return HAL_OK;
  if (length > 65536 - jb_string_size(text))
    return HAL_EOVERFLOW;
  return jb_string_append(text, (const char *)data, length);
}

static void check_cli(const char *tool, const jh_firmware_options_t *options,
                      bool success) {
  const char *args[] = {
      "firmware",   "read",          "--project", options->project,
      "--assembly", "fixture",       "--serial",  success ? "OK" : "FAIL",
      "--output",   options->output, "--json",    NULL};
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  const jb_process_options_t process = {
      tool, args, options->project, NULL, 5000, cli_output, NULL, &text};
  jb_process_result_t result;
  expect(jb_process_run(&process, &result) == HAL_OK && result.exited &&
             result.exit_code == (success ? 0u : 3u),
         "CLI exit status");
  jb_json_tree_t *tree = NULL;
  jb_json_tree_view_t root = {0};
  expect(jh_json_tree_from_text(jh_string_span(&text), &tree) == HAL_OK,
         "CLI emits JSON without tool diagnostics on stdout");
  expect(jb_json_tree_root(tree, &root) == HAL_OK, "CLI result root");
  jb_json_span_t state = {0};
  expect(jb_json_tree_text(jh_json_field(root, "deviceState"), &state) ==
                 HAL_OK &&
             jh_span_equal(state, jh_span(success ? "bootsel" : "unknown")),
         "CLI device state");
  if (success) {
    bool complete = false;
    jb_json_span_t size = {0};
    expect(jb_json_tree_get_bool(jh_json_field(root, "completeFlash"),
                                 &complete) == HAL_OK &&
               complete,
           "CLI marks a complete backup");
    expect(jb_json_tree_text(jh_json_field(root, "size"), &size) == HAL_OK &&
               jh_span_equal(size, jh_span("256")),
           "CLI includes storage beyond app region");
  }
  jb_json_tree_destroy(tree);
  jb_string_destroy(&text);
}

static void check_esp_cli(const char *tool,
                          const jh_firmware_options_t *options,
                          bool explicit_limits) {
  const char *args[] = {"firmware",
                        "read",
                        "--project",
                        options->project,
                        "--assembly",
                        options->assembly,
                        "--build-dir",
                        options->build_directory,
                        "--output",
                        options->output,
                        "--tool",
                        options->tool,
                        "--port",
                        explicit_limits ? "fast-port" : "fixture-port",
                        "--reset",
                        "--json",
                        "--baud",
                        "921600",
                        "--timeout-ms",
                        "0",
                        NULL};
  if (!explicit_limits)
    args[16] = NULL;
  jb_string_t text = {0};
  (void)jb_string_init(&text, jb_heap_allocator());
  const jb_process_options_t process = {
      tool, args, options->project, NULL, 5000, cli_output, NULL, &text};
  jb_process_result_t result;
  expect(jb_process_run(&process, &result) == HAL_OK && result.exited &&
             result.exit_code == 0,
         "ESP CLI completes");
  jb_json_tree_t *tree = NULL;
  jb_json_tree_view_t root = {0};
  expect(jh_json_tree_from_text(jh_string_span(&text), &tree) == HAL_OK &&
             jb_json_tree_root(tree, &root) == HAL_OK,
         "ESP CLI JSON");
  jb_json_span_t value = {0};
  expect(
      jb_json_tree_text(jh_json_field(root, "baudRate"), &value) == HAL_OK &&
          jh_span_equal(value, jh_span(explicit_limits ? "921600" : "115200")),
      "CLI reports effective baud rate");
  expect(jb_json_tree_text(jh_json_field(root, "timeoutMs"), &value) ==
                 HAL_OK &&
             jh_span_equal(value, jh_span(explicit_limits ? "0" : "242045")),
         "CLI distinguishes automatic timeout from explicit zero");
  jb_json_tree_destroy(tree);
  jb_string_destroy(&text);
}

int main(int argc, char **argv) {
  if (argc > 1 && (!strcmp(argv[1], "save") || !strcmp(argv[1], "-c") ||
                   !strcmp(argv[1], "-m")))
    return reader(argc, argv);
  if (argc != 4)
    return 2;
  (void)jb_string_init(&last_error, jb_heap_allocator());
  jb_temp_dir_t *temporary = NULL;
  if (jb_temp_dir_create(argv[1], &temporary) != HAL_OK)
    return 3;
  jb_vector_t strings = {0};
  (void)jh_list_init(&strings);
  const char *project = jb_temp_dir_path(temporary);
  const char *build = path(&strings, project, ".build/rp/fixture");
  const char *output = path(&strings, project, "output ż.bin");
  cache(&strings, build, project, "rp2040", argv[2]);
  const char *map = path(&strings, build, "firmware.map");
  write_text(
      map,
      "Memory Configuration\r\n\r\nName Origin Length Attributes\r\n"
      "FLASH 0x12340000 0x80 xr\r\nRAM 0x30000000 0x100 xrw\r\n"
      "HAL_LITTLEFS_FLASH 0x12340080 0x0 r\r\n"
      "HAL_EEPROM_FLASH 0x12340080 0x80 r\r\n"
      "Linker script and memory map\r\n"
      " 0x12340000 __jh_flash_device_start = __flash_binary_start - 0x0\r\n"
      " 0x100 __jh_flash_device_size = 0x100\r\n");
  jh_firmware_options_t options = {0};
  options.project = project;
  options.assembly = "fixture";
  options.output = output;
  options.serial = "OK";
  options.timeout_ms = 5000;
  /* An unrelated incomplete ESP build must not poison discovery. */
  const char *other = path(&strings, project, ".build/esp/old");
  write_text(path(&strings, other, "CMakeCache.txt"), "");
  write_text(path(&strings, other, "jh_esp_idf_artifacts.json"),
             "{\"assembly\":\"other-esp\",\"target\":\"esp32s3\"}");
  jh_firmware_result_t result = run(&options, HAL_OK);
  options.build_directory = other;
  result = run(&options, HAL_ECONFIG);
  expect(!result.attempted,
         "explicit mismatching ESP build rejected before reader");
  options.build_directory = NULL;
  expect(jb_fs_remove(other, true) == HAL_OK, "remove unrelated ESP fixture");
  result = run(&options, HAL_OK);
  expect(result.address == 0x12340000 && result.size == 256,
         "map selects default region");
  expect(
      !strcmp(
          result.sha256,
          "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880"),
      "independent SHA-256 of bytes 0..255");
  expect(!strcmp(result.device_state, "bootsel"), "RP reports BOOTSEL");
  options.output = "relative-firmware-test.bin";
  (void)run(&options, HAL_OK);
  expect(jb_fs_remove(options.output, false) == HAL_OK,
         "remove relative output");
  options.output = output;
  output_failures(&strings, &options);
  options.reset = true;
  result = run(&options, HAL_OK);
  expect(!result.published, "identical output is retained");
  options.reset = false;
  options.address = "0x12340080";
  options.size = "128";
  result = run(&options, HAL_OK);
  expect(!result.complete && result.size == 128,
         "mapped storage can be read separately");
  options.address = NULL;
  options.size = NULL;
  check_cli(argv[3], &options, true);
  const char *targets[] = {"rp2350-arm", "rp2350-riscv", "stm32g474"};
  for (size_t i = 0; i < COUNTOF(targets); ++i) {
    cache(&strings, build, project, targets[i], argv[2]);
    result = run(&options, HAL_OK);
    expect(result.target && !strcmp(result.target, targets[i]),
           "reader selected from target");
  }
  cache(&strings, build, project, "rp2040", argv[2]);
  write_text(output, "previous");
  check_cli(argv[3], &options, false);
  unchanged(output);
  const char *modes[] = {"FAIL", "SHORT", "LONG", "WAIT"};
  const hal_status_t statuses[] = {HAL_EIO, HAL_EIO, HAL_EOVERFLOW,
                                   HAL_ETIMEOUT};
  for (size_t i = 0; i < COUNTOF(modes); ++i) {
    options.serial = modes[i];
    options.timeout_ms = i == 3 ? 50 : 5000;
    result = run(&options, statuses[i]);
    expect(result.attempted, "tool failure came from child");
    if (i == 0 || i == 3)
      expect(!strcmp(result.device_state, "unknown"),
             "failed tool leaves state unknown");
    unchanged(output);
  }
  options.serial = "OK";
  options.timeout_ms = 5000;
  options.address = "0x30000000";
  options.size = "256";
  result = run(&options, HAL_EINVAL);
  expect(!result.attempted, "RAM rejected before process");
  options.address = "0x1233ffff";
  (void)run(&options, HAL_EINVAL);
  options.address = "0x123400ff";
  options.size = "2";
  (void)run(&options, HAL_EINVAL);
  options.address = "18446744073709551616";
  (void)run(&options, HAL_EINVAL);
  options.address = "0x12340000";
  options.size = "0";
  (void)run(&options, HAL_EINVAL);
  options.size = NULL;
  (void)run(&options, HAL_EINVAL);
  options.address = NULL;
  options.serial = "OK;reset";
  (void)run(&options, HAL_EINVAL);
  options.serial = "OK";
  unsigned cancel_calls = 0;
  options.canceled = canceled;
  options.context = &cancel_calls;
  (void)run(&options, HAL_ECANCELED);
  options.canceled = NULL;
  unchanged(output);
  options.assembly = "other";
  (void)run(&options, HAL_ENOENT);
  options.build_directory = build;
  (void)run(&options, HAL_ECONFIG);
  options.assembly = "fixture";
  options.variant = "missing";
  (void)run(&options, HAL_ECONFIG);
  options.variant = NULL;
  options.build_directory = NULL;
  const char *second = path(&strings, project, ".build/second");
  cache(&strings, second, project, "rp2040", argv[2]);
  (void)run(&options, HAL_ECONFIG);
  expect(jb_fs_remove(second, true) == HAL_OK, "remove duplicate build");
  /* Discovery sorts names, so this copy without firmware.map comes before
   * the real build; the duplicate must still win over its missing map. */
  const char *earlier = path(&strings, project, ".build/a-copy");
  cache(&strings, earlier, project, "rp2040", argv[2]);
  (void)run(&options, HAL_ECONFIG);
  expect(jb_fs_remove(earlier, true) == HAL_OK, "remove earlier duplicate");
  /* ESP's partition coordinates differ deliberately from the native map. */
  options.build_directory = build;
  options.serial = NULL;
  options.port = "fixture-port";
  options.reset = true;
  options.tool = argv[2];
  write_text(path(&strings, project, "hal_project_config.h"), "");
  write_text(
      path(&strings, build, "project.json"),
      "{\"projectConfigHeader\":{\"path\":\"hal_project_config.h\",\"sha256\":"
      "\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"},"
      "\"projectSourceSha256\":{}}");
  write_text(path(&strings, build, "hardware.json"),
             "{\"software\":{\"variant\":null}}");
  write_text(path(&strings, build, "flasher.json"),
             "{\"app\":{\"offset\":\"0x21000\"},\"flash_settings\":{\"flash_"
             "size\":\"1MB\"}}");
  /* Hash of this independent 32-byte partition fixture is fixed below. */
  unsigned char partition[32] = {0xaa, 0x50};
  jh_store_le32(partition + 4, 0x21000);
  jh_store_le32(partition + 8, 0x100);
  write_bytes(path(&strings, build, "partition.bin"), partition,
              sizeof partition);
  write_text(path(&strings, build, "jh_esp_idf_artifacts.json"),
             "{\"assembly\":\"fixture\",\"target\":\"esp32s3\",\"artifacts\":{"
             "\"projectConfig\":\"project.json\",\"resolvedBoardConfig\":"
             "\"hardware.json\","
             "\"flasherArgs\":\"flasher.json\"},\"configuration\":{"
             "\"partitionTable\":{"
             "\"path\":\"partition.bin\",\"sha256\":"
             "\"8c42e078d0929096f09577de85b7109add894dbe9d9f57c529fa51f1eaa8fa8"
             "a\"}}}");
  result = run(&options, HAL_OK);
  expect(result.address == 0 && result.size == 1024 * 1024 && result.complete &&
             !strcmp(result.device_state, "running"),
         "ESP complete flash and reset result");
  check_esp_cli(argv[3], &options, false);
  check_esp_cli(argv[3], &options, true);
  options.automatic_timeout = true;
  result = run(&options, HAL_OK);
  expect(result.timeout_ms == 242045 && result.baud_rate == 115200,
         "default UART budget covers framing and escaping for 1 MiB");
  options.baud_rate = 921600;
  options.port = "fast-port";
  result = run(&options, HAL_OK);
  expect(result.timeout_ms == 120000 && result.baud_rate == 921600,
         "explicit baud reaches reader and changes automatic timeout");
  options.canceled = cancel_before_read;
  options.baud_rate = 1;
  write_text(path(&strings, build, "flasher.json"),
             "{\"app\":{\"offset\":\"0x21000\"},\"flash_settings\":{\"flash_"
             "size\":\"64MB\"}}");
  result = run(&options, HAL_ECANCELED);
  expect(!result.attempted && result.size == 64u * 1024u * 1024u &&
             result.timeout_ms == UINT32_MAX,
         "large flash timeout saturates without overflow or unlimited wait");
  options.automatic_timeout = false;
  options.timeout_ms = 0;
  result = run(&options, HAL_ECANCELED);
  expect(result.timeout_ms == 0, "explicit unlimited timeout retained");
  options.timeout_ms = 1234;
  result = run(&options, HAL_ECANCELED);
  expect(result.timeout_ms == 1234, "explicit timeout overrides size budget");
  options.canceled = NULL;
  write_text(path(&strings, project, "hal_project_config.h"), "changed");
  result = run(&options, HAL_ECONFIG);
  expect(!result.attempted, "stale ESP project rejected before reader");
  expect(jb_temp_dir_remove(&temporary) == HAL_OK,
         "remove fixtures and reader staging");
  jh_list_destroy(&strings);
  jb_string_destroy(&last_error);
  return failures ? 1 : 0;
}
