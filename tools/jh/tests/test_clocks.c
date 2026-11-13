// The clock tree registry module on edited copies of config/hardware/clocks
// and on the clock source cases of tests/fixtures/device_tree/cases.json:
// frequencies that do not follow from the parameters, a backend on another
// target, extra trees of a backend whose clock the HAL never sets, a source
// the target has no input for, missing parameters, file names, the target
// set, the STM32G474 tree list, sdkconfig values and the sources a board
// gives a tree. The facts of the tracked registry (every check passes on the
// checkout) are in tests/test_clock_registry.cmake.
//
// Usage: jh_clocks_test <HAL checkout> <work directory>
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* symlink */
#endif
#include "jh_clock_registry.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include <jb/array.h>
#include <jb/file.h>
#include <jb/filesystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define LIMIT ((size_t)16 * 1024 * 1024)

static const char *root, *work;
static int failures;
static jh_board_registry_t boards;

static void expect(bool ok, int line, const char *what) {
  if (!ok) {
    fprintf(stderr, "test_clocks.c:%d: %s\n", line, what);
    ++failures;
  }
}
#define EXPECT(ok, what) expect((ok), __LINE__, (what))

/* snprintf that must not truncate: the test stops otherwise. */
static void formatted(char *out, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(out, size, format, args);
  va_end(args);
  if (written < 0 || (size_t)written >= size) {
    fprintf(stderr, "text does not fit %zu bytes: %s\n", size, format);
    exit(2);
  }
}

static void join(char *out, size_t size, const char *a, const char *b) {
  formatted(out, size, "%s/%s", a, b);
}

static char *read_text(const char *path) {
  jb_file_data_t data = {0};
  if (jb_file_read(path, LIMIT, &data) != HAL_OK) {
    fprintf(stderr, "cannot read %s\n", path);
    exit(2);
  }
  char *text = malloc(data.length + 1);
  if (text == NULL)
    exit(2);
  if (data.length != 0)
    memcpy(text, data.data, data.length);
  text[data.length] = '\0';
  jb_file_release(&data);
  return text;
}

static void write_text(const char *path, const char *text) {
  bool changed = false;
  if (jb_file_write_atomic(path, text, strlen(text), false, &changed) !=
      HAL_OK) {
    fprintf(stderr, "cannot write %s\n", path);
    exit(2);
  }
}

/* text with the first from replaced by to; the edit must apply. */
static char *replaced(const char *text, const char *from, const char *to) {
  const char *at = strstr(text, from);
  if (at == NULL) {
    fprintf(stderr, "edit does not apply: %s\n", from);
    exit(2);
  }
  const size_t head = (size_t)(at - text), tail = strlen(at + strlen(from));
  char *out = malloc(head + strlen(to) + tail + 1);
  if (out == NULL)
    exit(2);
  memcpy(out, text, head);
  memcpy(out + head, to, strlen(to));
  memcpy(out + head + strlen(to), at + strlen(from), tail + 1);
  return out;
}

static const char *const targets[] = {"esp32",    "esp32s3",    "mock",
                                      "rp2040",   "rp2350-arm", "rp2350-riscv",
                                      "stm32g474"};

/* work/name: a copy of config/hardware/clocks whose file target.json has the
 * edit from -> to (target NULL: no edit); skip leaves one file out. */
static void variant(char *dir, size_t size, const char *name,
                    const char *target, const char *from, const char *to,
                    const char *skip) {
  char source[4096], path[4096], clocks[4096];
  join(dir, size, work, name);
  (void)jb_fs_remove(dir, true);
  if (jb_fs_mkdir(dir, true) != HAL_OK)
    exit(2);
  join(clocks, sizeof clocks, root, "config/hardware/clocks");
  for (size_t i = 0; i < COUNTOF(targets); ++i) {
    char file[64];
    formatted(file, sizeof file, "%s.json", targets[i]);
    if (skip != NULL && strcmp(skip, targets[i]) == 0)
      continue;
    join(source, sizeof source, clocks, file);
    join(path, sizeof path, dir, file);
    char *text = read_text(source);
    if (target != NULL && strcmp(target, targets[i]) == 0) {
      char *edited = replaced(text, from, to);
      free(text);
      text = edited;
    }
    write_text(path, text);
    free(text);
  }
}

/* a == b, with \\ and / the same on Windows, where jh joins paths with \\. */
static bool same_text(const char *a, const char *b) {
#ifdef _WIN32
  for (; *a != '\0' && *b != '\0'; ++a, ++b)
    if (*a != *b && !((*a == '\\' || *a == '/') && (*b == '\\' || *b == '/')))
      return false;
  return *a == *b;
#else
  return strcmp(a, b) == 0;
#endif
}

static void message_is(const jh_failure_t *failure, const char *expected,
                       int line) {
  const char *got = jb_string_data(&failure->message);
  if (failure->status == HAL_OK || !same_text(got, expected)) {
    fprintf(stderr, "test_clocks.c:%d: got %s (exit %d)\n  expected %s\n", line,
            failure->status == HAL_OK ? "no failure" : got, failure->exit_code,
            expected);
    ++failures;
  } else if (failure->exit_code != 1) {
    fprintf(stderr, "test_clocks.c:%d: exit code %d, expected 1\n", line,
            failure->exit_code);
    ++failures;
  }
}

/* Load dir and run the registry and board checks. */
static hal_status_t checked(const char *dir, jh_failure_t *failure) {
  jh_clock_registry_t clocks;
  jh_clock_registry_init(&clocks);
  hal_status_t status =
      jh_clock_registry_load(&clocks, root, dir, LIMIT, failure);
  if (status == HAL_OK)
    status = jh_clock_check_registry(&clocks, &boards, failure);
  if (status == HAL_OK)
    status = jh_clock_check_boards(&clocks, &boards, failure);
  jh_clock_registry_destroy(&clocks);
  return status;
}

static void registry_case(const char *name, const char *target,
                          const char *from, const char *to,
                          const char *expected, int line) {
  char dir[4096];
  variant(dir, sizeof dir, name, target, from, to, NULL);
  jh_failure_t failure;
  jh_failure_init(&failure);
  (void)checked(dir, &failure);
  message_is(&failure, expected, line);
  jh_failure_destroy(&failure);
}

static void test_registry(void) {
  char dir[4096];
  variant(dir, sizeof dir, "unchanged", NULL, NULL, NULL, NULL);
  jh_failure_t failure;
  jh_failure_init(&failure);
  EXPECT(checked(dir, &failure) == HAL_OK, "the unchanged registry fails");
  jh_failure_destroy(&failure);

  registry_case(
      "rp-sys", "rp2040", "\"sys\": 125000000", "\"sys\": 133000000",
      "rp2040/default: frequencies {'ref': 12000000, 'sys': 133000000, "
      "'peri': 125000000, 'usb': 48000000, 'adc': 48000000} do not follow "
      "from the parameters {'ref': 12000000, 'sys': 125000000, 'peri': "
      "125000000, 'usb': 48000000, 'adc': 48000000}",
      __LINE__);
  registry_case(
      "stm32-fdcan", "stm32g474", "\"fdcan\": 80000000", "\"fdcan\": 160000000",
      "stm32g474/hse-160mhz: frequencies {'sysclk': 160000000, 'hclk': "
      "160000000, 'pclk1': 160000000, 'pclk2': 160000000, 'fdcan': 160000000, "
      "'i2c': 16000000} do not follow from the parameters {'sysclk': "
      "160000000, 'hclk': 160000000, 'pclk1': 160000000, 'pclk2': 160000000, "
      "'fdcan': 80000000, 'i2c': 16000000}",
      __LINE__);
  registry_case("esp-cpu", "esp32s3", "\"cpu\": 160000000",
                "\"cpu\": 240000000",
                "esp32s3/default: frequencies {'cpu': 240000000} do not follow "
                "from the parameters {'cpu': 160000000}",
                __LINE__);
  // The HAL leaves the Pico SDK, ESP-IDF and mock clocks to their defaults,
  // so another tree has no backend code even with consistent frequencies.
  static const char *const extra[][3] = {{"rp2040", "100mhz", "pico-sdk"},
                                         {"esp32s3", "240mhz", "esp-idf"},
                                         {"mock", "fast", "mock"}};
  for (size_t i = 0; i < COUNTOF(extra); ++i) {
    char to[512], expected[512];
    formatted(to, sizeof to,
              "\"trees\": {\"%s\": {\"description\": \"Not set by the "
              "HAL.\", \"backend\": \"%s\", \"requiredSources\": {}, "
              "\"frequenciesHz\": {}, \"parameters\": {}}, ",
              extra[i][1], extra[i][2]);
    formatted(expected, sizeof expected,
              "%s/%s: the HAL does not set the %s clock, so only its "
              "default tree runs",
              extra[i][0], extra[i][1], extra[i][2]);
    registry_case(extra[i][1], extra[i][0], "\"trees\": {", to, expected,
                  __LINE__);
  }
  registry_case("rp-hse", "rp2040", "\"requiredSources\": {",
                "\"requiredSources\": {\"hse\": {\"kind\": \"crystal\", "
                "\"frequencyHz\": 24000000}, ",
                "rp2040/default: rp2040 has no hse input for the crystal",
                __LINE__);
  registry_case("backend", "stm32g474", "\"backend\": \"stm32g474\"",
                "\"backend\": \"pico-sdk\"",
                "stm32g474/default: backend pico-sdk does not drive stm32g474",
                __LINE__);
  registry_case("parameters", "stm32g474", "\"pllN\": 85,", "",
                "stm32g474/default: missing parameters ['pllN']", __LINE__);
  registry_case(
      "two-parameters", "stm32g474", "\"pllN\": 85,\n        \"pllR\": 2,", "",
      "stm32g474/default: missing parameters ['pllN', 'pllR']", __LINE__);
  // Parameters are Python numbers: a float makes the formulas float, as
  // Python computes them (floor division of floats included).
  registry_case(
      "float", "rp2040", "\"pllSysPostdiv1\": 6", "\"pllSysPostdiv1\": 1.5",
      "rp2040/default: frequencies {'ref': 12000000, 'sys': 125000000, "
      "'peri': 125000000, 'usb': 48000000, 'adc': 48000000} do not follow "
      "from the parameters {'ref': 12000000, 'sys': 500000000.0, 'peri': "
      "500000000.0, 'usb': 48000000, 'adc': 48000000}",
      __LINE__);
  registry_case("string", "rp2040", "\"pllSysPostdiv1\": 6",
                "\"pllSysPostdiv1\": \"6\"",
                "rp2040/default: parameter pllSysPostdiv1 is '6', the "
                "frequencies need a number",
                __LINE__);
  registry_case(
      "negative", "rp2040", "\"pllSysPostdiv1\": 6", "\"pllSysPostdiv1\": -7",
      "rp2040/default: frequencies {'ref': 12000000, 'sys': 125000000, "
      "'peri': 125000000, 'usb': 48000000, 'adc': 48000000} do not follow "
      "from the parameters {'ref': 12000000, 'sys': -107142858, 'peri': "
      "-107142858, 'usb': 48000000, 'adc': 48000000}",
      __LINE__);
  registry_case("xtal", "esp32s3", "\"xtalFreqMhz\": 40", "\"xtalFreqMhz\": 26",
                "esp32s3/default: the xtal source does not match xtalFreqMhz",
                __LINE__);
  registry_case("pll-q", "stm32g474", "\"pllQ\": 4,", "",
                "stm32g474/hse-160mhz: missing parameters ['pllQ']", __LINE__);
  registry_case("zero", "stm32g474", "\"pllM\": 4", "\"pllM\": 0",
                "stm32g474/default: the parameters divide by zero or overflow "
                "64-bit arithmetic",
                __LINE__);
  variant(dir, sizeof dir, "float-source", "stm32g474",
          "\"frequencyHz\": 24000000", "\"frequencyHz\": 24000000.0", NULL);
  jh_failure_init(&failure);
  EXPECT(checked(dir, &failure) == HAL_OK,
         "an HSE source of 24000000.0 Hz fails, as Python accepted it");
  jh_failure_destroy(&failure);

  variant(dir, sizeof dir, "last-target", NULL, NULL, NULL, "stm32g474");
  jh_failure_init(&failure);
  (void)checked(dir, &failure);
  message_is(&failure,
             "clock registries ['esp32', 'esp32s3', 'mock', 'rp2040', "
             "'rp2350-arm', 'rp2350-riscv'] differ from targets ['esp32', "
             "'esp32s3', 'mock', 'rp2040', 'rp2350-arm', 'rp2350-riscv', "
             "'stm32g474']",
             __LINE__);
  jh_failure_destroy(&failure);
  variant(dir, sizeof dir, "targets", NULL, NULL, NULL, "esp32");
  jh_failure_init(&failure);
  (void)checked(dir, &failure);
  message_is(&failure,
             "clock registries ['esp32s3', 'mock', 'rp2040', 'rp2350-arm', "
             "'rp2350-riscv', 'stm32g474'] differ from targets ['esp32', "
             "'esp32s3', 'mock', 'rp2040', 'rp2350-arm', 'rp2350-riscv', "
             "'stm32g474']",
             __LINE__);
  jh_failure_destroy(&failure);

  // The schema failure with the smallest pointer is reported, as
  // SchemaSet.first_error picked it.
  static const char *const schema[][2] = {
      {"\"aaUnknown\": 1, \"schemaVersion\"", ":/aaUnknown: unknown field"},
      {"\"zzUnknown\": 1, \"schemaVersion\"",
       ":/trees/default/description: required field is missing"}};
  for (size_t i = 0; i < COUNTOF(schema); ++i) {
    char name[32], file[4096], expected_schema[4300];
    formatted(name, sizeof name, "schema%zu", i);
    variant(dir, sizeof dir, name, "rp2040", "\"schemaVersion\"", schema[i][0],
            NULL);
    join(file, sizeof file, dir, "rp2040.json");
    char *original = read_text(file);
    char *edited =
        replaced(original, "\"description\": ", "\"descriptionX\": ");
    free(original);
    (void)jb_fs_remove(file, false);
    write_text(file, edited);
    free(edited);
    formatted(expected_schema, sizeof expected_schema, "[JH-HW-SCHEMA] %s%s",
              file, schema[i][1]);
    jh_failure_init(&failure);
    (void)checked(dir, &failure);
    message_is(&failure, expected_schema, __LINE__);
    jh_failure_destroy(&failure);
  }

  // A registry for a target that does not exist.
  variant(dir, sizeof dir, "extra-target", NULL, NULL, NULL, NULL);
  char extra_path[4096], rp_path[4096];
  join(rp_path, sizeof rp_path, dir, "rp2040.json");
  join(extra_path, sizeof extra_path, dir, "zz.json");
  char *rp_text = read_text(rp_path);
  char *zz_text =
      replaced(rp_text, "\"target\": \"rp2040\"", "\"target\": \"zz\"");
  write_text(extra_path, zz_text);
  free(zz_text);
  free(rp_text);
  jh_failure_init(&failure);
  (void)checked(dir, &failure);
  message_is(&failure,
             "clock registries ['esp32', 'esp32s3', 'mock', 'rp2040', "
             "'rp2350-arm', 'rp2350-riscv', 'stm32g474', 'zz'] differ from "
             "targets ['esp32', 'esp32s3', 'mock', 'rp2040', 'rp2350-arm', "
             "'rp2350-riscv', 'stm32g474']",
             __LINE__);
  jh_failure_destroy(&failure);

  // A file is named after the target it declares.
  char clocks[4096], path[4096], expected[4200];
  join(dir, sizeof dir, work, "named");
  (void)jb_fs_remove(dir, true);
  EXPECT(jb_fs_mkdir(dir, true) == HAL_OK, "cannot create the named case");
  join(clocks, sizeof clocks, root, "config/hardware/clocks/rp2040.json");
  join(path, sizeof path, dir, "pico.json");
  char *text = read_text(clocks);
  write_text(path, text);
  free(text);
  jh_clock_registry_t registry;
  jh_clock_registry_init(&registry);
  jh_failure_init(&failure);
  EXPECT(jh_clock_registry_load(&registry, root, dir, LIMIT, &failure) !=
             HAL_OK,
         "a file named pico.json loads");
  formatted(expected, sizeof expected, "%s: file name must be rp2040.json",
            path);
  message_is(&failure, expected, __LINE__);
  jh_failure_destroy(&failure);
  jh_clock_registry_destroy(&registry);
}

/* The STM32G474 startup code builds the default and the HSE tree only, and
 * an ESP-IDF build's sdkconfig matches the default tree. */
static void test_backends(void) {
  char dir[4096], port[4096];
  join(port, sizeof port, root, "src/hal/impl/stm32g474/port");
  variant(dir, sizeof dir, "stm32-trees", "stm32g474", "\"hse-160mhz\"",
          "\"hse-170mhz\"", NULL);
  jh_clock_registry_t clocks;
  jh_clock_registry_init(&clocks);
  jh_failure_t failure;
  jh_failure_init(&failure);
  EXPECT(jh_clock_registry_load(&clocks, root, dir, LIMIT, &failure) == HAL_OK,
         "the renamed tree does not load");
  EXPECT(jh_clock_check_registry(&clocks, &boards, &failure) == HAL_OK,
         "a renamed but consistent tree fails the registry check");
  (void)jh_clock_check_stm32g474(&clocks, port, LIMIT, &failure);
  message_is(&failure,
             "stm32g474 trees ['default', 'hse-170mhz']: the startup code "
             "builds ['default', 'hse-160mhz'] only",
             __LINE__);
  jh_failure_destroy(&failure);
  jh_clock_registry_destroy(&clocks);

  // A third, consistent tree has no startup code either.
  variant(dir, sizeof dir, "stm32-extra", "stm32g474", "\"trees\": {",
          "\"trees\": {\"hse-170mhz\": {\"description\": \"x\", "
          "\"backend\": \"stm32g474\", \"requiredSources\": {}, "
          "\"frequenciesHz\": {\"sysclk\": 170000000, \"hclk\": 170000000, "
          "\"pclk1\": 170000000, \"pclk2\": 170000000, \"fdcan\": 170000000, "
          "\"i2c\": 16000000}, \"parameters\": {\"pllSource\": \"hsi16\", "
          "\"pllM\": 4, \"pllN\": 85, \"pllR\": 2, \"fdcanSource\": "
          "\"pclk1\"}}, ",
          NULL);
  jh_clock_registry_init(&clocks);
  jh_failure_init(&failure);
  EXPECT(jh_clock_registry_load(&clocks, root, dir, LIMIT, &failure) ==
                 HAL_OK &&
             jh_clock_check_registry(&clocks, &boards, &failure) == HAL_OK,
         "a consistent third STM32G474 tree fails the registry check");
  (void)jh_clock_check_stm32g474(&clocks, port, LIMIT, &failure);
  message_is(&failure,
             "stm32g474 trees ['default', 'hse-160mhz', 'hse-170mhz']: the "
             "startup code builds ['default', 'hse-160mhz'] only",
             __LINE__);
  jh_failure_destroy(&failure);
  jh_clock_registry_destroy(&clocks);

  variant(dir, sizeof dir, "unchanged", NULL, NULL, NULL, NULL);
  jh_clock_registry_init(&clocks);
  jh_failure_init(&failure);
  EXPECT(jh_clock_registry_load(&clocks, root, dir, LIMIT, &failure) == HAL_OK,
         "the unchanged registry does not load");
  EXPECT(jh_clock_check_stm32g474(&clocks, port, LIMIT, &failure) == HAL_OK,
         "the STM32G474 trees differ from the startup headers");
  // A header value past 64 bits is a plain mismatch, as Python's int is.
  char copy[4096], header[4096];
  join(copy, sizeof copy, work, "port");
  (void)jb_fs_remove(copy, true);
  EXPECT(jb_fs_mkdir(copy, true) == HAL_OK, "cannot create the port copy");
  static const char *const headers[] = {"stm32g474_clock.h",
                                        "stm32g474_clock_tree.h"};
  for (size_t i = 0; i < COUNTOF(headers); ++i) {
    char source[4096];
    join(source, sizeof source, port, headers[i]);
    join(header, sizeof header, copy, headers[i]);
    char *text = read_text(source);
    if (i == 0) {
      char *edited = replaced(text, "#define JH_G474_HSI_CLOCK_HZ 16000000u",
                              "#define JH_G474_HSI_CLOCK_HZ "
                              "99999999999999999999999u");
      free(text);
      text = edited;
    }
    write_text(header, text);
    free(text);
  }
  (void)jh_clock_check_stm32g474(&clocks, copy, LIMIT, &failure);
  message_is(&failure, "HSI16 frequency changed", __LINE__);
  jh_failure_destroy(&failure);
  jh_failure_init(&failure);
  char good[4096], fast[4096];
  join(good, sizeof good, work, "good-sdkconfig");
  join(fast, sizeof fast, work, "fast-sdkconfig");
  write_text(good,
             "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160\r\nCONFIG_XTAL_FREQ=40\r\n");
  write_text(fast,
             "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240\nCONFIG_XTAL_FREQ=40\n");
  EXPECT(jh_clock_check_esp_sdkconfig(&clocks, jh_span("esp32s3"), good, LIMIT,
                                      &failure) == HAL_OK,
         "a matching sdkconfig with CR LF lines fails");
  (void)jh_clock_check_esp_sdkconfig(&clocks, jh_span("esp32s3"), fast, LIMIT,
                                     &failure);
  message_is(&failure,
             "esp32s3: CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240, registry "
             "cpuFreqMhz=160",
             __LINE__);
  jh_failure_destroy(&failure);
  // A target whose default tree has no such parameter, where Python stopped
  // with a KeyError.
  jh_failure_init(&failure);
  (void)jh_clock_check_esp_sdkconfig(&clocks, jh_span("rp2040"), good, LIMIT,
                                     &failure);
  message_is(&failure, "rp2040: the default tree has no cpuFreqMhz", __LINE__);
  jh_failure_destroy(&failure);
  jh_failure_init(&failure);
  (void)jh_clock_check_esp_sdkconfig(&clocks, jh_span("nothere"), good, LIMIT,
                                     &failure);
  message_is(&failure,
             "--esp-sdkconfig names nothere, which has no clock registry",
             __LINE__);
  jh_failure_destroy(&failure);
  jh_clock_registry_destroy(&clocks);
}

/* Minimal Pico SDK headers below work/sdk with the settings the RP trees
 * name; edits change one of them. */
static const char pico_clocks[] =
    "// PLL settings for standard 125/150 MHz system clock.\n"
    "#if SYS_CLK_HZ == 125 * MHZ\n#define PLL_SYS_POSTDIV1 6\n#else\n"
    "#define PLL_SYS_POSTDIV1 5\n#endif\n"
    "#define PLL_SYS_VCO_FREQ_HZ                (1500 * MHZ)\n"
    "#define PLL_SYS_POSTDIV2 2\n#endif // SYS_CLK_KHZ == 125000\n"
    "#define PLL_USB_VCO_FREQ_HZ (1200 * MHZ)\n#define PLL_USB_POSTDIV1 5\n"
    "#define PLL_USB_POSTDIV2 5\n";
static const char pico_rp2040[] =
    "#define XOSC_HZ _u(12000000)\n#define PICO_USE_FASTEST_SUPPORTED_CLOCK 0\n"
    "#define SYS_CLK_HZ _u(125000000)\n";
static const char pico_rp2350[] =
    "#define XOSC_HZ _u(12000000)\n#define SYS_CLK_HZ _u(150000000)\n";

static void write_under(const char *base, const char *relative,
                        const char *text) {
  char path[4096], parent[4096];
  join(path, sizeof path, base, relative);
  formatted(parent, sizeof parent, "%s", path);
  char *slash = strrchr(parent, '/');
  if (slash != NULL)
    *slash = '\0';
  if (jb_fs_mkdir(parent, true) != HAL_OK)
    exit(2);
  (void)jb_fs_remove(path, false);
  write_text(path, text);
}

static void pico_case(const jh_clock_registry_t *clocks, const char *clocks_h,
                      const char *rp2040, const char *rp2350,
                      const char *expected, int line) {
  char sdk[4096];
  join(sdk, sizeof sdk, work, "sdk");
  write_under(sdk, "rp2_common/hardware_clocks/include/hardware/clocks.h",
              clocks_h);
  write_under(sdk, "rp2040/hardware_regs/include/hardware/platform_defs.h",
              rp2040);
  write_under(sdk, "rp2350/hardware_regs/include/hardware/platform_defs.h",
              rp2350);
  jh_failure_t failure;
  jh_failure_init(&failure);
  const hal_status_t status =
      jh_clock_check_pico_sdk(clocks, sdk, LIMIT, &failure);
  if (expected == NULL) {
    if (status != HAL_OK) {
      fprintf(stderr, "test_clocks.c:%d: %s\n", line,
              jb_string_data(&failure.message));
      ++failures;
    }
  } else {
    message_is(&failure, expected, line);
  }
  jh_failure_destroy(&failure);
}

static void test_pico_sdk(void) {
  char dir[4096];
  variant(dir, sizeof dir, "unchanged", NULL, NULL, NULL, NULL);
  jh_clock_registry_t clocks;
  jh_clock_registry_init(&clocks);
  jh_failure_t failure;
  jh_failure_init(&failure);
  EXPECT(jh_clock_registry_load(&clocks, root, dir, LIMIT, &failure) == HAL_OK,
         "the unchanged registry does not load");
  jh_failure_destroy(&failure);
  pico_case(&clocks, pico_clocks, pico_rp2040, pico_rp2350, NULL, __LINE__);
  char *edited = replaced(pico_clocks, "\n", "\r\n");
  char *crlf = edited;
  while (strstr(crlf, "\n#") != NULL) { /* every LF of the headers */
    char *next = replaced(crlf, "\n#", "\r\n\r#");
    if (crlf != edited)
      free(crlf);
    crlf = next;
  }
  pico_case(&clocks, crlf, pico_rp2040, pico_rp2350, NULL, __LINE__);
  if (crlf != edited)
    free(crlf);
  free(edited);
  edited = replaced(pico_rp2040, "_u(125000000)", "_u(133000000)");
  pico_case(&clocks, pico_clocks, edited, pico_rp2350,
            "rp2040: Pico SDK default is 133000000 Hz", __LINE__);
  free(edited);
  edited = replaced(pico_rp2350, "_u(150000000)", "_u(200000000)");
  pico_case(&clocks, pico_clocks, pico_rp2040, edited,
            "rp2350-arm: Pico SDK default is 200000000 Hz", __LINE__);
  free(edited);
  edited = replaced(pico_clocks,
                    "PLL settings for standard 125/150 MHz system clock.",
                    "PLL settings.");
  pico_case(&clocks, edited, pico_rp2040, pico_rp2350,
            "Pico SDK standard PLL block moved", __LINE__);
  free(edited);
  edited = replaced(pico_clocks, "PLL_USB_POSTDIV1 5", "PLL_USB_POSTDIV1 4");
  pico_case(&clocks, edited, pico_rp2040, pico_rp2350,
            "Pico SDK PLL_USB_POSTDIV1 is no longer 5", __LINE__);
  free(edited);
  edited = replaced(pico_rp2040, "#define PICO_USE",
                    "#define XOSC_HZ _u(12000000)\n#define PICO_USE");
  pico_case(&clocks, pico_clocks, edited, pico_rp2350,
            "rp2040 platform_defs.h: expected one match of '#define XOSC_HZ "
            "_u\\\\((\\\\d+)\\\\)', found ['12000000', '12000000']",
            __LINE__);
  free(edited);
  jh_clock_registry_destroy(&clocks);

  // 750 MHz / 3 / 2 is 125 MHz too, but not the SDK's PLL_SYS.
  variant(dir, sizeof dir, "pll", "rp2040", "\"pllSysVcoHz\": 1500000000,",
          "\"pllSysVcoHz\": 750000000,", NULL);
  char file[4096];
  join(file, sizeof file, dir, "rp2040.json");
  char *text = read_text(file);
  char *edited_pll =
      replaced(text, "\"pllSysPostdiv1\": 6", "\"pllSysPostdiv1\": 3");
  free(text);
  (void)jb_fs_remove(file, false);
  write_text(file, edited_pll);
  free(edited_pll);
  jh_clock_registry_init(&clocks);
  jh_failure_init(&failure);
  EXPECT(jh_clock_registry_load(&clocks, root, dir, LIMIT, &failure) ==
                 HAL_OK &&
             jh_clock_check_registry(&clocks, &boards, &failure) == HAL_OK,
         "a consistent 750 MHz / 3 / 2 tree fails the registry check");
  jh_failure_destroy(&failure);
  pico_case(&clocks, pico_clocks, pico_rp2040, pico_rp2350,
            "rp2040: PLL_SYS differs", __LINE__);
  jh_clock_registry_destroy(&clocks);
}

/* Build files that define a Pico SDK clock macro: the first in sorted path
 * order is reported; other suffixes and linked directories are not read. */
static void test_override(void) {
  char scan[4096];
  join(scan, sizeof scan, work, "scan");
  (void)jb_fs_remove(scan, true);
  write_under(scan, "src/z.hpp", "#define XOSC_HZ 1\n");
  write_under(scan, "src/.h", "#define XOSC_HZ 1\n");
#ifndef _WIN32
  // Path.rglob of Python 3.12 does not enter a linked directory.
  write_under(scan, "outside/y.h", "#define SYS_CLK_HZ 1\n");
  char link_path[4096], target_path[4096];
  join(link_path, sizeof link_path, scan, "src/linked");
  join(target_path, sizeof target_path, scan, "outside");
  EXPECT(symlink(target_path, link_path) == 0, "cannot create the link");
  static const char *const loop_bases[] = {"cmake", "link_libraries", "src",
                                           "config", "examples"};
  for (size_t i = 0; i < COUNTOF(loop_bases); ++i) {
    formatted(link_path, sizeof link_path, "%s/keep.txt", loop_bases[i]);
    write_under(scan, link_path, "");
    formatted(link_path, sizeof link_path, "%s/%s/loop-a.txt", scan,
              loop_bases[i]);
    formatted(target_path, sizeof target_path, "%s/%s/loop-b.txt", scan,
              loop_bases[i]);
    EXPECT(symlink("loop-b.txt", link_path) == 0,
           "cannot create first loop link");
    EXPECT(symlink("loop-a.txt", target_path) == 0,
           "cannot create second loop link");
  }
#endif
  jh_failure_t failure;
  jh_failure_init(&failure);
  EXPECT(jh_clock_check_no_hal_override(scan, LIMIT, &failure) == HAL_OK,
         "an ignored suffix, linked directory or link loop stopped the scan");
#ifndef _WIN32
  for (size_t i = 0; i < COUNTOF(loop_bases); ++i) {
    formatted(link_path, sizeof link_path, "%s/%s/loop-a.txt", scan,
              loop_bases[i]);
    formatted(target_path, sizeof target_path, "%s/%s/loop-b.txt", scan,
              loop_bases[i]);
    EXPECT(unlink(link_path) == 0, "cannot remove first loop link");
    EXPECT(unlink(target_path) == 0, "cannot remove second loop link");
  }
#endif
  write_under(scan, "src/b.h", "#define XOSC_HZ 1\n");
  write_under(scan, "src/a/x.h", "int a;\n#define SYS_CLK_HZ 1\n");
  (void)jh_clock_check_no_hal_override(scan, LIMIT, &failure);
  message_is(&failure,
             "src/a/x.h sets a Pico SDK clock macro: #define SYS_CLK_HZ",
             __LINE__);
  jh_failure_destroy(&failure);
  jh_failure_init(&failure);
  write_under(
      scan, "cmake/x.cmake",
      "target_compile_definitions(x PUBLIC\n  A=1\n  PLL_USB_POSTDIV1=2)\n");
  (void)jh_clock_check_no_hal_override(scan, LIMIT, &failure);
  message_is(&failure,
             "cmake/x.cmake sets a Pico SDK clock macro: "
             "compile_definitions(x PUBLIC\n  A=1\n  PLL_USB_POSTDIV1",
             __LINE__);
  jh_failure_destroy(&failure);
  (void)jb_fs_remove(scan, true);

  // A match that backtracks over 2 MB passes PCRE2's default step limit:
  // the scan still finds it, as Python's re did, instead of reading the
  // stopped search as no match.
  write_under(scan, "src/keep.txt", "");
  static const char head[] = "compile_definitions(SYS_CLK_HZ ";
  const size_t filler = (size_t)2 * 1024 * 1024;
  char *big = malloc(sizeof head + filler + 2);
  if (big == NULL)
    exit(2);
  memcpy(big, head, sizeof head - 1);
  for (size_t i = 0; i < filler; i += 2) {
    big[sizeof head - 1 + i] = 'x';
    big[sizeof head + i] = ' ';
  }
  memcpy(big + sizeof head - 1 + filler, "\n", 2);
  write_under(scan, "cmake/big.cmake", big);
  free(big);
  jh_failure_init(&failure);
  (void)jh_clock_check_no_hal_override(scan, LIMIT, &failure);
  message_is(&failure,
             "cmake/big.cmake sets a Pico SDK clock macro: "
             "compile_definitions(SYS_CLK_HZ",
             __LINE__);
  jh_failure_destroy(&failure);
  (void)jb_fs_remove(scan, true);
}

static jb_json_tree_t *parse(const char *text) {
  const jb_json_limits_t limits = {LIMIT,   100000, 64,   LIMIT,
                                   1048576, 100000, LIMIT};
  const jb_json_tree_limits_t tree_limits = {100000, 64, LIMIT, LIMIT};
  jb_json_tree_t *tree = NULL;
  if (jb_json_tree_parse(jh_span(text), &limits, &tree_limits, 0, &tree,
                         NULL) != HAL_OK)
    exit(2);
  return tree;
}

static jb_json_tree_view_t root_of(const jb_json_tree_t *tree) {
  jb_json_tree_view_t view = {0};
  (void)jb_json_tree_root(tree, &view);
  return view;
}

/* RM0440 7.2.1/7.2.4: a crystal takes OSC_IN and OSC_OUT, bypass only
 * OSC_IN. */
static const char stm32_inputs[] =
    "{\"hse\": {\"crystal\": [\"PF0\", \"PF1\"], \"external-clock\": "
    "[\"PF0\"]}, \"lse\": {\"crystal\": [\"PC14\", \"PC15\"], "
    "\"external-clock\": [\"PC14\"]}}";
static const char hse_tree[] =
    "{\"requiredSources\": {\"hse\": {\"kind\": \"crystal\", \"frequencyHz\": "
    "24000000}}}";

/* The source errors of sources for hse_tree on stm32_inputs: their count,
 * and the first pointer and message when expected. */
static void sources_case(const char *sources, size_t count, const char *pointer,
                         const char *message, int line) {
  jb_json_tree_t *tree = parse(hse_tree), *inputs = parse(stm32_inputs);
  jb_json_tree_t *given = parse(sources);
  jb_vector_t errors = {0};
  jh_failure_t failure;
  jh_failure_init(&failure);
  bool ok =
      jh_clock_source_errors_init(&errors) == HAL_OK &&
      jh_clock_source_errors(root_of(given), root_of(tree), root_of(inputs),
                             &errors, &failure) == HAL_OK &&
      jb_vector_size(&errors) == count;
  const jh_clock_source_error_t *first =
      ok && count != 0 ? jb_vector_const_at(&errors, 0) : NULL;
  if (ok && first != NULL && pointer != NULL)
    ok = strcmp(jb_string_data(&first->pointer), pointer) == 0;
  if (ok && first != NULL && message != NULL)
    ok = strcmp(jb_string_data(&first->message), message) == 0;
  if (!ok) {
    fprintf(stderr, "test_clocks.c:%d: %zu source errors, first %s: %s\n", line,
            jb_vector_size(&errors),
            first != NULL ? jb_string_data(&first->pointer) : "-",
            first != NULL ? jb_string_data(&first->message) : "-");
    ++failures;
  }
  jh_failure_destroy(&failure);
  jh_clock_source_errors_destroy(&errors);
  jb_json_tree_destroy(given);
  jb_json_tree_destroy(inputs);
  jb_json_tree_destroy(tree);
}

static void test_sources(void) {
  static const char crystal[] =
      "{\"hse\": {\"kind\": \"crystal\", \"frequencyHz\": %s, \"pins\": [%s]}}";
  char text[512];
  formatted(text, sizeof text, crystal, "24000000",
            "{\"domain\": \"soc-gpio\", \"id\": \"PF0\"}, "
            "{\"domain\": \"soc-gpio\", \"id\": \"PF1\"}");
  sources_case(text, 0, NULL, NULL, __LINE__);
  sources_case("{}", 1, "/hse", "no hse source", __LINE__);
  sources_case("{\"hse\": null}", 1, "/hse", "no hse source", __LINE__);
  formatted(text, sizeof text, crystal, "25000000",
            "{\"domain\": \"soc-gpio\", \"id\": \"PF0\"}, "
            "{\"domain\": \"soc-gpio\", \"id\": \"PF1\"}");
  sources_case(text, 1, "/hse/frequencyHz",
               "hse is a crystal at 25000000 Hz, the tree needs a crystal at "
               "24000000 Hz",
               __LINE__);
  // An empty or partial list would hide the crystal from the reservations.
  static const char *const pins[] = {
      "", "{\"domain\": \"soc-gpio\", \"id\": \"PF0\"}",
      "{\"domain\": \"soc-gpio\", \"id\": \"PF0\"}, {\"domain\": "
      "\"soc-gpio\", \"id\": \"PF1\"}, {\"domain\": \"soc-gpio\", \"id\": "
      "\"PA0\"}"};
  for (size_t i = 0; i < COUNTOF(pins); ++i) {
    formatted(text, sizeof text, crystal, "24000000", pins[i]);
    sources_case(text, 1, "/hse/pins", NULL, __LINE__);
  }
  formatted(text, sizeof text, crystal, "24000000",
            "{\"domain\": \"soc-gpio\", \"id\": \"PF0\"}");
  sources_case(text, 1, "/hse/pins",
               "the crystal on hse occupies ['PF0', 'PF1'], the board lists "
               "['PF0']",
               __LINE__);

  // A bypass clock occupies OSC_IN alone; a target without the input
  // refuses it.
  jb_json_tree_t *bypass =
      parse("{\"kind\": \"external-clock\", "
            "\"frequencyHz\": 24000000, \"pins\": "
            "[{\"domain\": \"soc-gpio\", \"id\": \"PF0\"}]}");
  jb_json_tree_t *stm32 = parse(stm32_inputs);
  jb_json_tree_t *rp = parse("{\"xosc\": {\"crystal\": []}}");
  jb_string_t problem = {0};
  EXPECT(jb_string_init(&problem, jb_heap_allocator()) == HAL_OK &&
             jh_board_clock_source_problem(root_of(stm32), jh_span("hse"),
                                           root_of(bypass),
                                           &problem) == HAL_OK &&
             jb_string_size(&problem) == 0,
         "STM32G474 refuses an HSE bypass clock on PF0");
  EXPECT(jh_board_clock_source_problem(root_of(rp), jh_span("hse"),
                                       root_of(bypass), &problem) == HAL_OK &&
             strcmp(jb_string_data(&problem),
                    "the target has no hse input for the external-clock") == 0,
         "a target without an HSE input accepts a bypass clock");
  jb_string_destroy(&problem);
  jb_json_tree_destroy(rp);
  jb_json_tree_destroy(stm32);
  jb_json_tree_destroy(bypass);
}

/* The semantic clock source cases of tests/fixtures/device_tree: the first
 * error of the source file for the named tree of its clock registry is at
 * the expected pointer. */
static void test_fixture_cases(void) {
  char fixtures[4096], path[4096];
  join(fixtures, sizeof fixtures, root, "tests/fixtures/device_tree");
  join(path, sizeof path, fixtures, "cases.json");
  jb_json_tree_t *cases_tree = NULL, *inputs = parse(stm32_inputs);
  jb_json_tree_view_t cases_root = {0}, cases = {0};
  jh_failure_t failure;
  jh_failure_init(&failure);
  EXPECT(jh_check_load_object(path, LIMIT, &cases_tree, &cases_root,
                              &failure) == HAL_OK &&
             jh_json_member(cases_root, "cases", &cases),
         "cannot read the device tree cases");
  size_t checked_cases = 0;
  for (size_t i = 0; i < jh_json_count(cases); ++i) {
    const jb_json_tree_view_t item = jh_json_item(cases, i);
    jb_json_tree_view_t value = {0};
    jb_json_span_t file = {NULL, 0}, registry = {NULL, 0}, name = {NULL, 0},
                   at = {NULL, 0};
    if (!jh_json_member(item, "clockTree", &value) ||
        !jh_json_string(value, &name))
      continue;
    (void)jh_json_member(item, "file", &value);
    (void)jh_json_string(value, &file);
    (void)jh_json_member(item, "clockRegistry", &value);
    (void)jh_json_string(value, &registry);
    (void)jh_json_member(item, "at", &value);
    (void)jh_json_string(value, &at);
    char file_path[4096], registry_path[4096], relative[512];
    formatted(relative, sizeof relative, "%.*s", (int)file.length, file.data);
    join(file_path, sizeof file_path, fixtures, relative);
    formatted(relative, sizeof relative, "%.*s", (int)registry.length,
              registry.data);
    join(registry_path, sizeof registry_path, fixtures, relative);
    char *sources_text = read_text(file_path),
         *registry_text = read_text(registry_path);
    jb_json_tree_t *sources = parse(sources_text),
                   *trees = parse(registry_text);
    jb_json_tree_view_t all = {0}, tree = {0};
    (void)jh_json_member(root_of(trees), "trees", &all);
    (void)jb_json_tree_find(all, name, &tree);
    jb_vector_t errors = {0};
    const bool ok =
        jh_clock_source_errors_init(&errors) == HAL_OK &&
        jh_clock_source_errors(root_of(sources), tree, root_of(inputs), &errors,
                               &failure) == HAL_OK &&
        jb_vector_size(&errors) != 0 &&
        jh_span_equal(jh_string_span(&((const jh_clock_source_error_t *)
                                           jb_vector_const_at(&errors, 0))
                                          ->pointer),
                      at);
    if (!ok) {
      fprintf(stderr, "test_clocks.c: case %s: first error not at %.*s\n",
              file_path, (int)at.length, at.data);
      ++failures;
    }
    ++checked_cases;
    jh_clock_source_errors_destroy(&errors);
    jb_json_tree_destroy(trees);
    jb_json_tree_destroy(sources);
    free(registry_text);
    free(sources_text);
  }
  EXPECT(checked_cases != 0, "no clock source case in cases.json");
  jh_failure_destroy(&failure);
  jb_json_tree_destroy(inputs);
  jb_json_tree_destroy(cases_tree);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: jh_clocks_test <HAL checkout> <work directory>\n");
    return 2;
  }
  root = argv[1];
  char work_dir[4096];
  join(work_dir, sizeof work_dir, argv[2], "clocks-test");
  work = work_dir;
  (void)jb_fs_remove(work, true);
  if (jb_fs_mkdir(work, true) != HAL_OK)
    return 2;
  char boards_root[4096];
  join(boards_root, sizeof boards_root, root, "boards");
  jh_failure_t failure;
  jh_failure_init(&failure);
  jh_board_registry_init(&boards);
  if (jh_board_registry_load(&boards, root, boards_root, LIMIT, &failure) !=
      HAL_OK) {
    fprintf(stderr, "board registry: %s\n", jb_string_data(&failure.message));
    return 2;
  }
  jh_failure_destroy(&failure);
  test_registry();
  test_backends();
  test_pico_sdk();
  test_override();
  test_sources();
  test_fixture_cases();
  jh_board_registry_destroy(&boards);
  (void)jb_fs_remove(work, true);
  if (failures != 0) {
    fprintf(stderr, "%d clock registry checks failed\n", failures);
    return 1;
  }
  puts("clock registry checks passed");
  return 0;
}
