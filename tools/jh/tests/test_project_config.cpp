// jh::config::read_project_config on headers of a private directory: values,
// includes with #pragma once, #error, declarations, uncertainty, the int64_t
// range, invalid literals and the limits of jh where Python has none. A native
// test, so the gate also runs it under Valgrind and the sanitizers.
//
// Usage: jh_config_test <work dir> [<C compiler> <JaszczurHAL root>]
// With a GCC or Clang compiler the reader is also compared with the
// compiler's preprocessor (-E -dM): the equivalence headers below and every
// hal_project_config.h of the checkout outside tests/hardware, for each
// target selector and variant.
#include "jh_json_file.h"
#include "jh_project_config.h"
#include "jh_python_repr.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <jb/process.h>
#include <map>
#include <string>
#include <vector>

namespace {
using jh::config::integer_kind;
using jh::config::project_config;
using jh::config::read_project_config;

int failures = 0;
void expect(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "project config: %s\n", what);
    ++failures;
  }
}

std::string write(const std::string &directory, const char *name,
                  const std::string &bytes) {
  const std::string path = jh::absolute_path(name, directory);
  bool changed = false;
  expect(jb_file_write_atomic(path.c_str(), bytes.data(), bytes.size(), false,
                              &changed) == HAL_OK,
         name);
  return path;
}

// The failure of reading header, or HAL_OK.
jh::failure rejection(const std::string &header,
                      const std::vector<std::string> &predefined = {},
                      const jh::config::read_limits &limits = {}) {
  try {
    (void)read_project_config(header, predefined, {}, limits);
  } catch (const jh::failure &error) {
    return error;
  }
  return {HAL_OK, 0, {}, {}, {}, {}};
}

bool has(const std::string &text, const char *part) {
  return text.find(part) != std::string::npos;
}

void values_and_includes(const std::string &directory) {
  write(directory, "common.h",
        "#pragma once\n#ifdef SHARED\n#error read twice\n#endif\n"
        "#define SHARED 2000U // shared timeout\n");
  const auto header = write(
      directory, "hal_project_config.h",
      "#include \"common.h\"\n#include \"common.h\"\n"
      "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040) X(HAL_TARGET_MOCK)\n"
      "#define JH_PROJECT_VARIANTS(X) X(WIDE, \"Wide panel\", PANEL = 2, "
      "EXTRA)\n"
      "#define A (-7 / 2) /* C division */\n#define B (-7 % 2)\n"
      "#if PANEL == 2 && defined(EXTRA) ? 1 : (1 / 0)\n#define "
      "HAL_ENABLE_ST7796S\n"
      "#else\n#define HAL_ENABLE_ILI9341\n#endif\n"
      "#if defined(SDK_ONLY)\n#define HAL_ENABLE_WIFI\n#endif\n"
      "#define SIZE (FLASH_BYTES / 256)\n#define KIB(x) ((x) * 1024)\n");
  const project_config config = read_project_config(
      header, {"HAL_TARGET_RP2040", "PANEL=2", "EXTRA"}, {});
  expect(config.files.size() == 2 && !config.stopped,
         "#pragma once reads the included file once");
  expect(config.macros.find("SHARED")->body == "2000U" &&
             config.integer("SHARED").value == 2000,
         "included value without its comment");
  expect(config.integer("A").value == -3 && config.integer("B").value == -1,
         "C division and remainder");
  expect(config.macros.find("HAL_ENABLE_ST7796S") != nullptr &&
             config.macros.find("HAL_ENABLE_ILI9341") == nullptr,
         "variant switch condition");
  expect(config.targets.size() == 2 && config.variants.size() == 1 &&
             config.variants[0].definitions[0] == "PANEL=2",
         "declarations");
  expect(config.uncertain("HAL_ENABLE_WIFI") ==
             std::set<std::string>{"SDK_ONLY"},
         "a name decided by an unknown identifier");
  expect(config.unresolved("SIZE") == std::set<std::string>{"FLASH_BYTES"} &&
             config.integer("SIZE").kind == integer_kind::none,
         "a value built from an undefined name");
  expect(config.macros.find("KIB")->function_like &&
             config.macros.find("KIB")->parameters.size() == 1,
         "function-like macro");
  expect(config.features().size() == 1, "header features");
  // The header first, in read order, then the included file.
  const auto &directives = config.directives;
  const auto &shared = directives.back();
  const auto kib =
      std::find_if(directives.begin(), directives.end(),
                   [](const auto &item) { return item.name == "KIB"; });
  expect(directives.size() == 10 &&
             directives.front().name == "JH_PROJECT_TARGETS" &&
             shared.name == "SHARED" && shared.text == "2000U" &&
             shared.line == 5 && !shared.undef && kib != directives.end() &&
             kib->text == "(x) ((x) * 1024)" && kib->line == 16,
         "directives of every file read, in any branch");
}

void stop_and_rejections(const std::string &directory) {
  const auto stopped = write(directory, "stop.h",
                             "#define BEFORE 1\n#if 1\n#error \"stop here\"\n"
                             "#define AFTER 1\n");
  const project_config config = read_project_config(stopped, {}, {});
  expect(config.stopped && config.error_message == "\"stop here\"" &&
             config.macros.find("AFTER") == nullptr,
         "#error stops the read");
  const struct {
    const char *text, *part;
  } rejected[] = {
      {"#include <stdint.h>\n", "only #include"},
      {"#if 1\n#define A\n", "unterminated #if"},
      {"#endif\n", "#endif without #if"},
      {"#define F(x) x\n#if F(1)\n#endif\n", "function-like macro F"},
      {"#if 1 +\n#endif\n", "expected operand"},
      {"#if 1 && (1 / 0)\n#endif\n", "division by zero"},
      {"#if 1 @\n#endif\n", "cannot evaluate ' @'"},
      {"#if FOO\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n",
       "unconditionally"},
      {"#define JH_PROJECT_VARIANTS(X) X(bench, \"Bench\", B)\n",
       "capital letters"},
      {"#definé X\n", "non-ASCII character after #defin"},
  };
  for (const auto &item : rejected) {
    const auto error = rejection(write(directory, "rejected.h", item.text));
    expect(error.status == HAL_ECONFIG && error.exit_code == 2 &&
               has(error.message, item.part),
           item.part);
  }
  const auto value = rejection(stopped, {"A =1"});
  expect(value.status == HAL_ECONFIG &&
             value.message == "predefined: [JH-CFG-VALUE] invalid definition "
                              "'A =1'",
         "invalid predefined definition");
}

void range_and_literals(const std::string &directory) {
  const auto wide =
      write(directory, "wide.h",
            "#define BIG 0xFFFFFFFFFFFFFFFFull\n"
            "#define LOW (-9223372036854775807 - 1)\n"
            "#if 0 && 0xFFFFFFFFFFFFFFFF\n#define SKIPPED\n#endif\n");
  const project_config config = read_project_config(wide, {}, {});
  expect(config.integer("BIG").kind == integer_kind::out_of_range,
         "a value outside int64_t has no integer");
  expect(config.integer("LOW").value == INT64_MIN, "INT64_MIN");
  expect(config.macros.find("SKIPPED") == nullptr, "an operand C skips");
  const auto overflow =
      rejection(write(directory, "overflow.h", "#if 1 << 63\n#endif\n"));
  expect(overflow.status == HAL_EOVERFLOW && overflow.exit_code == 4 &&
             has(overflow.message, "1 << 63 exceeds the signed 64-bit range"),
         "a computed condition outside int64_t");
  // Every computed operation outside int64_t, with its operands.
  const struct {
    const char *condition, *detail;
  } ranges[] = {
      {"0xFFFFFFFFFFFFFFFF > 0", "integer 0xFFFFFFFFFFFFFFFF"},
      {"9223372036854775807 + 1", "9223372036854775807 + 1"},
      {"(-9223372036854775807 - 1) - 1", "-9223372036854775808 - 1"},
      {"-(-9223372036854775807 - 1)", "-(-9223372036854775808)"},
      {"4294967296 * 4294967296", "4294967296 * 4294967296"},
      {"1 << 63", "1 << 63"},
      {"(-9223372036854775807 - 1) / -1", "-9223372036854775808 / -1"},
  };
  for (const auto &item : ranges) {
    const auto header = write(directory, "range.h",
                              std::string("#if ") + item.condition +
                                  "\n#define HIT 1\n#endif\n");
    const auto error = rejection(header);
    expect(error.status == HAL_EOVERFLOW && error.exit_code == 4 &&
               error.message == header + ":1: [JH-CFG-SCOPE] " + item.detail +
                                    " exceeds the signed 64-bit range of jh",
           item.condition);
  }
  expect(read_project_config(
             write(directory, "exact.h",
                   "#if (1 << 62) * -2 == -9223372036854775807 - 1 && "
                   "(-1 << 63) < 0 && (-9223372036854775807 - 1) % -1 == 0 && "
                   "-5 >> 1 == -3 && 1 >> 64 == 0 && -1 >> 99 == -1\n"
                   "#define HIT 1\n#endif\n"),
             {}, {})
                 .macros.find("HIT") != nullptr,
         "edges of the int64_t range");
  // Python's int(text, 8) raises ValueError, which no reader turns into "no
  // value", also in a macro the build only reads.
  const auto octal = write(directory, "octal.h", "#define BAD 09\n");
  try {
    (void)read_project_config(octal, {}, {}).integer("BAD");
    expect(false, "invalid octal literal accepted");
  } catch (const jh::failure &error) {
    expect(error.status == HAL_EPROTO && has(error.message, "'09'"),
           "invalid octal literal");
  }
}

void limits(const std::string &directory) {
  const auto nested =
      rejection(write(directory, "nested.h",
                      "#if " + std::string(300, '(') + "1" +
                          std::string(300, ')') + "\n#endif\n"));
  expect(nested.status == HAL_EOVERFLOW && has(nested.message, "nests more"),
         "condition nesting limit");
  // Each macro uses the previous one twice: 2^40 paths, 40 macros.
  std::string chain;
  for (int level = 1; level <= 40; ++level)
    chain += "#define M" + std::to_string(level) + " M" +
             std::to_string(level - 1) + " + M" + std::to_string(level - 1) +
             "\n";
  const std::string uses = "#if 1\n#elif M40\n#endif\n";
  const project_config sdk = read_project_config(
      write(directory, "chain.h", "#define M0 SDK_VALUE\n" + chain + uses), {},
      {});
  const auto value = sdk.value("M40");
  expect(value.unresolved == std::set<std::string>{"SDK_VALUE"} &&
             value.integer.kind == integer_kind::none &&
             sdk.unresolved("M39") == value.unresolved,
         "dependency tracking scans each macro once");
  // Dependency tracking alone spends the query budget too.
  const project_config tracked = read_project_config(
      write(directory, "tracked.h", "#define M0 SDK_VALUE\n" + chain), {}, {},
      jh::config::read_limits{100});
  expect(tracked.unresolved("M2") == std::set<std::string>{"SDK_VALUE"},
         "a small dependency question");
  try {
    (void)tracked.unresolved("M40");
    expect(false, "dependency tracking ignored its budget");
  } catch (const jh::failure &error) {
    expect(error.status == HAL_EOVERFLOW, "dependency budget status");
  }
  expect(tracked.unresolved("M2") == std::set<std::string>{"SDK_VALUE"},
         "a dependency question after an exceeded one");
  const jh::config::read_limits small{5000};
  const auto exponential =
      rejection(write(directory, "exponential.h",
                      "#define M0 1\n" + chain + "#if M40\n#endif\n"),
                {}, small);
  expect(exponential.status == HAL_EOVERFLOW &&
             has(exponential.message,
                 "expansion and dependency tracking exceed 5000 tokens"),
         "expansion work limit");
  // Each query has a budget of its own: the queries that exceed it fail
  // alone, and a constant reads the same before and after them.
  const std::string constant = "#define HAL_RP_CORE1_STACK_SIZE 2048\n";
  for (const bool constant_first : {true, false}) {
    const std::string text = constant_first
                                 ? constant + "#define M0 1\n" + chain
                                 : "#define M0 1\n" + chain + constant;
    const project_config queries =
        read_project_config(write(directory, "queries.h", text), {}, {}, small);
    for (int round = 0; round < 3; ++round) {
      for (const char *name : {"M40", "M39"}) {
        try {
          (void)queries.value(name);
          expect(false, "a query ignored its budget");
        } catch (const jh::failure &error) {
          expect(error.status == HAL_EOVERFLOW &&
                     has(error.message, "exceed 5000 tokens"),
                 "query budget");
        }
      }
      const auto stack = queries.value("HAL_RP_CORE1_STACK_SIZE").integer;
      expect(stack.kind == integer_kind::value && stack.value == 2048 &&
                 queries.integer("M3").value == 8,
             "queries do not share a budget");
    }
  }
  const auto invalid = rejection(write(directory, "invalid.h", "\xff\n"));
  expect(invalid.status == HAL_EPROTO && invalid.exit_code == 2,
         "invalid UTF-8");
}

// ── Cases of the former Python reader tests ─────────────────────────────────

const char *const defaults_header = R"H(#pragma once
#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE (8 * 1024)
#endif
#define BASE 4
#if BASE * 2 == 8 && !defined(MISSING)
#define HAL_ENABLE_KV
#elif 1
#define HAL_ENABLE_I2C
#endif
#if defined(HAL_TARGET_RP2350_ARM) || defined HAL_TARGET_RP2040
#define HAL_RP_CORE0_STACK_SIZE (BASE * 1024)
#else
#define HAL_RP_CORE0_STACK_SIZE 2048
#endif
#ifdef HAL_ENABLE_KV
#undef BASE
#define BASE 5
#endif
#if 0
#if 1 / 0
#define NEVER
#endif
#elif BASE == 5
#define HAL_ENABLE_SPI 1
#endif
#if 1
#define FIRST_TAKEN
#elif 0
#define NEVER_A
#elif 1
#define NEVER_B
#else
#define NEVER_C
#endif
#define KEPT 1
#if 0
#undef KEPT
#endif
)H";

const char *const variants_header = R"H(#pragma once
#define JH_PROJECT_VARIANTS(X) \
  X(PROBE, "Smoke test", PROBE_ONLY=1) \
  X(WIDE, "Wide panel", PANEL = 2, EXTRA)
#if !PROBE_ONLY
#define HAL_ENABLE_DMA_PWM_AUDIO
#endif
#if PANEL == 2
#define HAL_ENABLE_ST7796S
#else
#define HAL_ENABLE_ILI9341
#endif
#if defined(EXTRA) && PANEL > 1 ? 1 : 0
#define HAL_ENABLE_APP_TASK1
#endif
#if PANEL == 2 && !defined(HAL_TARGET_RP2350_ARM)
#error "wide panel needs RP2350"
#endif
)H";

const char *const literals_header = R"H(#pragma once
#if 0 && (1 / 0)
#define A 1
#endif
#if 1 || (1 % 0)
#define B 1
#endif
#if 1 ? 2 : (1 / 0)
#define C 1
#endif
#if 0 ? (1 / 0) : 0
#define D 1
#endif
#if 1u && 0x10 == 16 && 'a' == 97 && 0b11 == 3
#define HAL_ENABLE_CRC
#endif
#if 0 && (1 << -1)
#define E 1
#endif
#if 1 ? 1 : (1 >> -1)
#define F 1
#endif
)H";

const char *const arithmetic_header = R"H(#pragma once
#define A (-7 / 2) // C truncates
#define B (-7 % 2) /* the sign of the dividend */
#define C (1 << 4 | 0x0F & 0b1010)
#define D 'A'
#if A == -3 && B == -1 && C == 26 && D == 65 && 010 == 8 && 10u == 10
#define HAL_ENABLE_CRC
#endif
#if ~0 == -1 && !(3 > 4) && (2 >= 2) && (1 != 2)
#define HAL_ENABLE_TIME
#endif
)H";

const struct {
  const char *name, *text;
} equivalence[] = {{"defaults_and_conditions", defaults_header},
                   {"variant_switches", variants_header},
                   {"short_circuit_and_literals", literals_header},
                   {"arithmetic", arithmetic_header}};

const std::vector<std::vector<std::string>> predefined_sets = {
    {"HAL_TARGET_RP2040"},
    {"HAL_TARGET_RP2350_ARM"},
    {"HAL_TARGET_STM32G474", "PROBE_ONLY=1"},
    {"HAL_TARGET_RP2350_ARM", "PANEL=2", "EXTRA"},
    {"HAL_TARGET_RP2040", "PANEL=2"}};

std::vector<std::string> names_of(const project_config &config) {
  std::vector<std::string> names;
  for (const auto *item : config.features())
    names.push_back(item->name);
  return names;
}

void former_python_cases(const std::string &directory) {
  // Values the build reads are integers, evaluated the way C does.
  const auto arithmetic =
      write(directory, "arithmetic/hal_project_config.h", arithmetic_header);
  const project_config values = read_project_config(arithmetic, {}, {});
  expect(values.integer("A").value == -3 && values.integer("B").value == -1,
         "C division");
  expect(values.integer("C").value == 26, "bitwise precedence");
  const auto defaults_path =
      write(directory, "defaults/hal_project_config.h", defaults_header);
  const project_config defaults =
      read_project_config(defaults_path, {"HAL_TARGET_RP2040"}, {});
  expect(defaults.integer("HAL_RP_FLASH_EEPROM_SIZE").value == 8192,
         "nested arithmetic");
  // BASE was redefined under a feature that MISSING decides, so neither the
  // reader nor CMake can know the stack size the compiler will use.
  expect(defaults.integer("HAL_RP_CORE0_STACK_SIZE").kind ==
                 integer_kind::none &&
             defaults.unresolved("HAL_RP_CORE0_STACK_SIZE") ==
                 std::set<std::string>{"MISSING"},
         "a value behind an undecided redefinition");
  const project_config later = read_project_config(
      write(directory, "later/hal_project_config.h",
            "#define BASE 4\n#define SIZE (BASE * 1024)\n#undef BASE\n"
            "#define BASE 5\n"),
      {}, {});
  expect(later.integer("SIZE").value == 5120 &&
             later.unresolved("SIZE").empty(),
         "a macro uses the later value");
  expect(names_of(defaults) ==
             std::vector<std::string>{"HAL_ENABLE_KV", "HAL_ENABLE_SPI"},
         "features are the header's own definitions");
  const auto feature =
      names_of(read_project_config(defaults_path, {"HAL_ENABLE_CRC"}, {}));
  expect(std::find(feature.begin(), feature.end(), "HAL_ENABLE_CRC") ==
             feature.end(),
         "a -D feature is not a header feature");

  // Variants come from the X-macro, whatever the target; a formatted
  // "PANEL = 2" reaches the build as the -D form. CRLF reads the same.
  const auto variants_path =
      write(directory, "variants/hal_project_config.h", variants_header);
  const project_config variants = read_project_config(variants_path, {}, {});
  expect(variants.variants.size() == 2 && variants.variants[0].id == "PROBE" &&
             variants.variants[0].description == "Smoke test" &&
             variants.variants[0].definitions ==
                 std::vector<std::string>{"PROBE_ONLY=1"} &&
             variants.variants[1].definitions ==
                 std::vector<std::string>{"PANEL=2", "EXTRA"},
         "variants");
  std::string crlf;
  for (const char c : std::string(variants_header))
    crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
  const auto crlf_path =
      write(directory, "variants-crlf/hal_project_config.h", crlf);
  for (const std::vector<std::string> &predefined :
       {std::vector<std::string>{},
        std::vector<std::string>{"HAL_TARGET_RP2350_ARM", "PANEL=2",
                                 "EXTRA"}}) {
    const project_config lf =
        read_project_config(variants_path, predefined, {});
    const project_config cr = read_project_config(crlf_path, predefined, {});
    expect(cr.variants.size() == lf.variants.size() &&
               cr.variants[1].definitions == lf.variants[1].definitions &&
               names_of(cr) == names_of(lf),
           "a CRLF header reads the same");
  }
  const project_config blocked =
      read_project_config(variants_path, {"HAL_TARGET_RP2040", "PANEL=2"}, {});
  expect(blocked.stopped && has(blocked.error_message, "RP2350"),
         "variant #error");
  expect(variants.uncertain("HAL_ENABLE_ST7796S").empty(),
         "a declared variant switch is a known input");

  // The file's own include guard does not make a declaration conditional.
  const project_config guarded = read_project_config(
      write(directory, "guarded/hal_project_config.h",
            "#ifndef HAL_PROJECT_CONFIG_H\n#define HAL_PROJECT_CONFIG_H\n"
            "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040)\n"
            "#define JH_PROJECT_VARIANTS(X) X(PROBE, \"Probe\", PROBE_ONLY=1)\n"
            "#if PROBE_ONLY\n#define HAL_ENABLE_CRC\n#endif\n#endif\n"),
      {"HAL_TARGET_RP2040", "PROBE_ONLY=1"}, {});
  expect(guarded.targets == std::vector<std::string>{"HAL_TARGET_RP2040"} &&
             guarded.variants.size() == 1 &&
             guarded.macros.find("HAL_ENABLE_CRC") != nullptr,
         "declarations inside an include guard");

  // Macros decided by an identifier the reader cannot see.
  const project_config uncertain = read_project_config(
      write(directory, "uncertain/hal_project_config.h",
            "#if defined(PICO_RP2040)\n#define HAL_RP_CORE1_STACK_SIZE 4096\n"
            "#endif\n#ifdef SDK_ONLY\n#define HAL_ENABLE_WIFI\n#else\n"
            "#define HAL_ENABLE_UART\n#endif\n#if defined(OTHER)\n"
            "#define LOCAL_ONLY 1\n#endif\n#undef HAL_RP_CORE1_STACK_SIZE\n"
            "#define HAL_RP_CORE1_STACK_SIZE 2048\n"),
      {}, {});
  const std::set<std::string> sdk_only{"SDK_ONLY"};
  expect(uncertain.uncertain("HAL_ENABLE_WIFI") == sdk_only &&
             uncertain.uncertain("HAL_ENABLE_UART") == sdk_only &&
             uncertain.uncertain("LOCAL_ONLY") ==
                 std::set<std::string>{"OTHER"} &&
             uncertain.uncertain("HAL_RP_CORE1_STACK_SIZE").empty(),
         "dependencies of inactive, else and later exact definitions");

  // Values the compiler computes from names this configuration does not
  // define have no value here, also through a local alias.
  const project_config sdk = read_project_config(
      write(directory, "sdk/hal_project_config.h",
            "#define FLASH_BYTES PICO_FLASH_SIZE_BYTES\n"
            "#define HAL_RP_FLASH_EEPROM_SIZE (FLASH_BYTES / 256)\n"
            "#define HAL_RP_CORE1_STACK_SIZE (2u * 1024u)\n"
            "#define HAL_EEPROM_TYPE EEPROM_TYPE_FLASH\n"
            "#define KIB(x) ((x) * 1024)\n"
            "#define HAL_RP_FLASH_LITTLEFS_SIZE KIB(64)\n"
            "#define TEXT \"PICO_FLASH_SIZE_BYTES\"\n"),
      {}, {});
  expect(sdk.integer("HAL_RP_FLASH_EEPROM_SIZE").kind == integer_kind::none &&
             sdk.unresolved("HAL_RP_FLASH_EEPROM_SIZE") ==
                 std::set<std::string>{"PICO_FLASH_SIZE_BYTES"} &&
             sdk.integer("HAL_RP_CORE1_STACK_SIZE").value == 2048 &&
             sdk.unresolved("HAL_EEPROM_TYPE") ==
                 std::set<std::string>{"EEPROM_TYPE_FLASH"} &&
             sdk.unresolved("HAL_RP_FLASH_LITTLEFS_SIZE") ==
                 std::set<std::string>{"KIB"} &&
             sdk.unresolved("TEXT").empty(),
         "unresolved values: SDK alias, C name, function-like macro, string");

  // A condition decided through a local alias depends on what the alias
  // expands to; numbers and character literals are not names.
  const project_config aliased =
      read_project_config(write(directory, "aliased/hal_project_config.h",
                                "#define ON_RP2040 PICO_RP2040\n"
                                "#if ON_RP2040\n#define HAL_ENABLE_KV\n#endif\n"
                                "#if 1u && 0x10 == 16 && 'a' == 97\n#define "
                                "HAL_ENABLE_CRC\n#endif\n"),
                          {"HAL_TARGET_RP2040"}, {});
  const project_config relayed = read_project_config(
      write(directory, "relayed/hal_project_config.h",
            "#if defined(SDK_SWITCH)\n#define LOCAL_SWITCH 1\n#endif\n"
            "#if LOCAL_SWITCH\n#define HAL_ENABLE_KV\n#endif\n"),
      {}, {});
  expect(aliased.uncertain("HAL_ENABLE_KV") ==
                 std::set<std::string>{"PICO_RP2040"} &&
             relayed.uncertain("HAL_ENABLE_KV") ==
                 std::set<std::string>{"SDK_SWITCH"} &&
             aliased.macros.find("HAL_ENABLE_CRC") != nullptr &&
             aliased.uncertain("HAL_ENABLE_CRC").empty(),
         "conditions through an alias and a relayed switch");

  // C does not evaluate the operand a decided && or || skips, nor the ?:
  // branch it does not take.
  const project_config short_circuit = read_project_config(
      write(directory, "short/hal_project_config.h",
            "#if 0 && (1 / 0)\n#define A\n#endif\n#if 1 || (1 % 0)\n#define B\n"
            "#endif\n#if 1 ? 2 : (1 / 0)\n#define C\n#endif\n"
            "#if 0 ? (1 / 0) : 0\n#define D\n#endif\n"),
      {}, {});
  expect(!short_circuit.macros.find("A") && short_circuit.macros.find("B") &&
             short_circuit.macros.find("C") && !short_circuit.macros.find("D"),
         "short-circuit evaluation");

  // Quoted includes resolve next to the including file; nothing else does.
  const auto shared = write(directory, "shared/common.h",
                            "#pragma once\n#define SHARED_TIMEOUT 2000U\n");
  const auto including = write(directory, "project/hal_project_config.h",
                               "#pragma once\n#include \"../shared/common.h\"\n"
                               "#include \"../shared/common.h\"\n");
  const project_config included = read_project_config(including, {}, {});
  expect(included.macros.find("SHARED_TIMEOUT")->body == "2000U" &&
             included.files.size() == 2 &&
             included.files[1] ==
                 std::filesystem::u8path(shared).lexically_normal().u8string(),
         "relative include and its inputs");

  const struct {
    const char *text, *part;
  } rejected[] = {
      {"#ifndef G\n#define G\n#else\n#define JH_PROJECT_VARIANTS(X) X(A, "
       "\"A\", B)\n#endif\n",
       "unconditionally"},
      {"#ifndef G\n#define G\n#endif\n#ifndef H\n#define "
       "JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n",
       "unconditionally"},
      {"#ifndef G\n#define OTHER\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", "
       "B)\n#endif\n",
       "unconditionally"},
      {"#define JH_PROJECT_VARIANTS X(A, \"A\", B)\n", "one parameter"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, \"A\")\n",
       "at least one definition"},
      {"#define JH_PROJECT_VARIANTS(X) X(A-B, \"A\", B)\n", "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B) X(A, \"Again\", C)\n",
       "must be unique"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, A, B)\n", "string description"},
      {"#define JH_PROJECT_VARIANTS(X) X(Bench, \"Bench\", B)\n",
       "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(_BENCH, \"Bench\", B)\n",
       "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(1BENCH, \"Bench\", B)\n",
       "capital letters"},
      {"#define JH_PROJECT_VARIANTS(X) X(A, \"A\", HAL_TARGET_RP2040)\n",
       "invalid definition"},
      {"#define JH_PROJECT_VARIANTS(X) Y(a, \"A\", B)\n", "only X("},
  };
  for (const auto &item : rejected) {
    const auto error = rejection(write(directory, "rejected2.h", item.text));
    expect(error.status == HAL_ECONFIG && has(error.message, item.part),
           item.part);
  }

  const project_config empty = read_project_config(
      jh::absolute_path("missing.h", directory), {"HAL_TARGET_MOCK"}, {});
  expect(empty.files.empty() && empty.variants.empty() &&
             empty.macros.find("HAL_TARGET_MOCK") != nullptr,
         "a missing header is empty and predefined macros stay visible");

  // Active negative shifts follow the GCC extension; see the preprocessor
  // comparison for GCC itself.
  expect(read_project_config(write(directory, "shifts/hal_project_config.h",
                                   "#if (1 << -1) == 0 && (8 >> -1) == 16\n"
                                   "#define GCC_NEGATIVE_SHIFT 1\n#endif\n"),
                             {}, {})
                 .macros.find("GCC_NEGATIVE_SHIFT") != nullptr,
         "GCC negative shifts");
}

// ── Comparison with the compiler's preprocessor ─────────────────────────────

struct captured {
  std::string out, err;
};

hal_status_t capture(void *user, jb_process_stream_t stream,
                     const unsigned char *data, size_t length) {
  auto &text = *static_cast<captured *>(user);
  (stream == JB_PROCESS_STDOUT ? text.out : text.err)
      .append(reinterpret_cast<const char *>(data), length);
  return HAL_OK;
}

std::string normalized(const std::string &text) {
  std::string out;
  bool space = false;
  for (const char c : text) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
        c == '\v') {
      space = !out.empty();
      continue;
    }
    if (space)
      out += ' ';
    space = false;
    out += c;
  }
  return out;
}

/* Macros the compiler defines after the header, or false when it stops. */
bool compiler_macros(const std::string &compiler, const std::string &work,
                     const std::string &header,
                     const std::vector<std::string> &predefined,
                     std::map<std::string, std::string> *macros) {
  const std::string probe =
      write(work, "probe/probe.c", "#include \"hal_project_config.h\"\n");
  std::vector<std::string> words{
      "-E",  "-dM",
      "-x",  "c",
      probe, "-I" + std::filesystem::u8path(header).parent_path().u8string()};
  for (const auto &item : predefined)
    words.push_back("-D" + item);
  std::vector<const char *> arguments;
  for (const auto &word : words)
    arguments.push_back(word.c_str());
  arguments.push_back(nullptr);
  captured text;
  jb_process_options_t options{};
  options.executable = compiler.c_str();
  options.arguments = arguments.data();
  options.working_directory = work.c_str();
  options.timeout_ms = 60000;
  options.output = capture;
  options.context = &text;
  jb_process_result_t result{};
  jh::require(jb_process_run(&options, &result), 3, "cannot run the compiler");
  if (result.exit_code != 0) {
    expect(has(text.err, "#error"), "the preprocessor failed without #error");
    return false;
  }
  size_t begin = 0;
  while (begin < text.out.size()) {
    size_t end = text.out.find('\n', begin);
    if (end == std::string::npos)
      end = text.out.size();
    const std::string line = text.out.substr(begin, end - begin);
    begin = end + 1;
    if (line.rfind("#define ", 0) != 0)
      continue;
    size_t name_end = 8;
    while (name_end < line.size() && line[name_end] != ' ' &&
           line[name_end] != '(')
      ++name_end;
    size_t value = name_end;
    if (value < line.size() && line[value] == '(')
      value = line.find(')', value) + 1;
    (*macros)[line.substr(8, name_end - 8)] =
        normalized(value < line.size() ? line.substr(value) : "");
  }
  return true;
}

/* The reader and the preprocessor agree on #error and on every macro the
 * reader defines or the header or the definitions name. */
void compare_with_compiler(const std::string &compiler, const std::string &work,
                           const std::string &header, const std::string &text,
                           const std::vector<std::string> &predefined,
                           const std::string &label) {
  const project_config config = read_project_config(header, predefined, {});
  std::map<std::string, std::string> expected;
  const bool defined =
      compiler_macros(compiler, work, header, predefined, &expected);
  if (config.stopped != !defined) {
    std::fprintf(stderr, "project config: %s: #error differs\n", label.c_str());
    ++failures;
    return;
  }
  if (!defined)
    return;
  std::string joined;
  for (const auto &item : predefined)
    joined += item + ' ';
  std::set<std::string> names;
  for (const auto *item : config.macros.ordered())
    names.insert(item->name);
  for (const auto &item : expected)
    if (has(text, item.first.c_str()) || has(joined, item.first.c_str()))
      names.insert(item.first);
  for (const auto &name : names) {
    const jh::config::macro *item = config.macros.find(name);
    const auto found = expected.find(name);
    if (item == nullptr || found == expected.end() ||
        (!item->function_like && normalized(item->body) != found->second)) {
      std::fprintf(stderr,
                   "project config: %s: %s differs from the preprocessor\n",
                   label.c_str(), name.c_str());
      ++failures;
    }
  }
}

std::string file_text(const std::string &path) {
  jh::file_owner owner;
  jh::require(jb_file_read(path.c_str(), 16 * 1024 * 1024, &owner.data), 3,
              "cannot read a project header");
  return std::string(reinterpret_cast<const char *>(owner.data.data),
                     owner.data.length);
}

void preprocessor(const std::string &compiler, const std::string &root,
                  const std::string &directory) {
  for (const auto &item : equivalence) {
    const auto header = write(
        directory,
        (std::string("equivalence/") + item.name + "/hal_project_config.h")
            .c_str(),
        item.text);
    for (const auto &predefined : predefined_sets)
      compare_with_compiler(compiler, directory, header, item.text, predefined,
                            item.name);
  }
  // GCC accepts negative shift counts in active operands; Clang does not.
  const auto shifts =
      jh::absolute_path("shifts/hal_project_config.h", directory);
  std::map<std::string, std::string> gcc;
  if (compiler_macros(compiler, directory, shifts, {}, &gcc) &&
      gcc.count("__GNUC__") != 0 && gcc.count("__clang__") == 0)
    expect(gcc.count("GCC_NEGATIVE_SHIFT") != 0,
           "GCC preprocessor negative-shift behaviour changed");

  // Every hal_project_config.h of the checkout outside tests/hardware, as each
  // target selector and variant reads it.
  const std::set<std::string> skipped{".build", ".git", "build", "third_party",
                                      "node_modules"};
  std::vector<std::string> headers;
  const auto base = std::filesystem::u8path(root);
  for (auto it = std::filesystem::recursive_directory_iterator(base);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    const auto relative = it->path().lexically_relative(base);
    if (it->is_directory() &&
        (skipped.count(it->path().filename().u8string()) != 0 ||
         relative == std::filesystem::u8path("tests/hardware"))) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file() &&
        it->path().filename() == "hal_project_config.h")
      headers.push_back(it->path().u8string());
  }
  std::sort(headers.begin(), headers.end());
  expect(headers.size() > 20, "too few project headers in the checkout");
  const std::vector<std::string> selectors{
      "HAL_TARGET_RP2040",       "HAL_TARGET_RP2350_ARM",
      "HAL_TARGET_RP2350_RISCV", "HAL_TARGET_STM32G474",
      "HAL_TARGET_ESP32",        "HAL_TARGET_ESP32_S3",
      "HAL_TARGET_MOCK"};
  size_t builds = 0;
  for (const auto &header : headers) {
    const std::string text = file_text(header);
    const auto declared = read_project_config(header, {}, {}).variants;
    for (const auto &selector : selectors) {
      std::vector<std::vector<std::string>> sets{{selector}};
      for (const auto &variant : declared) {
        sets.push_back({selector});
        sets.back().insert(sets.back().end(), variant.definitions.begin(),
                           variant.definitions.end());
      }
      for (const auto &predefined : sets) {
        compare_with_compiler(compiler, directory, header, text, predefined,
                              header);
        ++builds;
      }
    }
  }
  std::printf(
      "the reader matches the preprocessor on %zu headers, %zu builds\n",
      headers.size(), builds);
}

void repr() {
  using jh::python_repr;
  expect(python_repr("a'b") == "\"a'b\"" && python_repr("a\"'b") == "'a\"\\'b'",
         "repr quotes");
  expect(python_repr("\t\x01\xc2\xa0\xc4\x85") == "'\\t\\x01\\xa0\xc4\x85'",
         "repr escapes");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2 && argc != 4) {
    std::fputs("usage: jh_config_test <work directory> [<C compiler> "
               "<JaszczurHAL root>]\n",
               stderr);
    return 2;
  }
  try {
    const std::string directory =
        jh::absolute_path("project-config-test", argv[1]);
    bool changed = false;
    expect(jb_file_write_atomic(jh::absolute_path("marker", directory).c_str(),
                                "", 0, false, &changed) == HAL_OK,
           "work directory");
    values_and_includes(directory);
    stop_and_rejections(directory);
    range_and_literals(directory);
    limits(directory);
    former_python_cases(directory);
    repr();
    if (argc == 4)
      preprocessor(argv[2], argv[3], directory);
  } catch (const jh::failure &error) {
    std::fprintf(stderr, "project config: unexpected failure: %s\n",
                 error.message.c_str());
    return 1;
  }
  if (failures != 0)
    return 1;
  std::puts("project configuration reader tests passed");
  return 0;
}
