#include "jh_clock_registry.h"
#include "jh_json_check.h"
#include "jh_json_file.h"
#include "jh_python_repr.h"
#include <jb/array.h>
#include <jb/file.h>
#include <jb/filesystem.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#define HSI16_HZ INT64_C(16000000)

const jh_clock_tree_component_t jh_clock_stm32g474_trees[] = {
    {"default", NULL}, {"hse-160mhz", "stm32g474-hse-24mhz"}};
const size_t jh_clock_stm32g474_tree_count = COUNTOF(jh_clock_stm32g474_trees);

/* ---- Backends ---- */

typedef enum { PICO_SDK, STM32G474, ESP_IDF, MOCK } backend_kind_t;

/* A backend of clock_registry.py: the targets it drives, the parameters its
 * trees need and whether the HAL sets only its default tree. */
typedef struct {
  const char *name;
  backend_kind_t kind;
  const char *const *targets;
  size_t target_count;
  const char *const *parameters;
  size_t parameter_count;
  bool default_only;
} backend_t;

static const char *const pico_targets[] = {"rp2040", "rp2350-arm",
                                           "rp2350-riscv"};
static const char *const pico_parameters[] = {
    "pllSysVcoHz", "pllSysPostdiv1", "pllSysPostdiv2",
    "pllUsbVcoHz", "pllUsbPostdiv1", "pllUsbPostdiv2"};
static const char *const stm32_targets[] = {"stm32g474"};
static const char *const stm32_parameters[] = {"pllSource", "pllM", "pllN",
                                               "pllR", "fdcanSource"};
static const char *const esp_targets[] = {"esp32", "esp32s3"};
static const char *const esp_parameters[] = {"cpuFreqMhz", "xtalFreqMhz"};
static const char *const mock_targets[] = {"mock"};

static const backend_t backends[] = {
    {"pico-sdk", PICO_SDK, pico_targets, COUNTOF(pico_targets), pico_parameters,
     COUNTOF(pico_parameters), true},
    {"stm32g474", STM32G474, stm32_targets, COUNTOF(stm32_targets),
     stm32_parameters, COUNTOF(stm32_parameters), false},
    {"esp-idf", ESP_IDF, esp_targets, COUNTOF(esp_targets), esp_parameters,
     COUNTOF(esp_parameters), true},
    {"mock", MOCK, mock_targets, COUNTOF(mock_targets), NULL, 0, true},
};

static const backend_t *find_backend(jb_json_span_t name) {
  for (size_t i = 0; i < COUNTOF(backends); ++i)
    if (jh_span_equal(name, jh_span(backends[i].name)))
      return &backends[i];
  return NULL;
}

static bool backend_drives(const backend_t *backend, jb_json_span_t target) {
  for (size_t i = 0; backend != NULL && i < backend->target_count; ++i)
    if (jh_span_equal(target, jh_span(backend->targets[i])))
      return true;
  return false;
}

/* ---- Messages ---- */

static hal_status_t text_init(jb_string_t *text) {
  return jb_string_init(text, jb_heap_allocator());
}

/* A ClockError: HAL_ECONFIG with exit code 1 and the joined parts. */
static hal_status_t fail_check(jh_failure_t *failure,
                               const jb_json_span_t *parts, size_t count) {
  return jh_fail(failure, HAL_ECONFIG, 1, parts, count);
}

/* fail_check with a message built in text, or the failure to build it. */
static hal_status_t fail_built(jh_failure_t *failure, hal_status_t status,
                               const jb_string_t *text) {
  return status == HAL_OK ? fail_check(failure, JH_PARTS(jh_string_span(text)))
                          : jh_fail_storage(failure, status);
}

/* str() of a JSON value as an f-string prints it: a string unquoted, any
 * other value as its repr. */
static void put_str(jb_string_t *out, hal_status_t *status,
                    jb_json_tree_view_t value) {
  jb_json_span_t text = {NULL, 0};
  if (value.tree == NULL) /* a member the data does not have */
    jh_put(out, status, "None");
  else if (jh_json_string(value, &text))
    jh_put_span(out, status, text);
  else if (*status == HAL_OK)
    *status = jh_python_repr_json_append(out, value);
}

static void put_repr(jb_string_t *out, hal_status_t *status,
                     jb_json_span_t text) {
  if (*status == HAL_OK)
    *status = jh_python_repr_append(out, text);
}

/* ---- Numbers as Python compares them ---- */

/* A JSON value Python treats as a number: int (bool included) or float. */
typedef struct {
  bool is_float;
  int64_t integer;
  double real;
} number_t;

/* int(): an integer token within int64_t or a bool. */
static bool as_integer(jb_json_tree_view_t value, int64_t *out) {
  if (jh_json_type(value) == JB_JSON_BOOL) {
    bool flag = false;
    (void)jb_json_tree_get_bool(value, &flag);
    *out = flag ? 1 : 0;
    return true;
  }
  return jh_json_int64(value, out);
}

/* The number json.loads makes of value; false for a string, null,
 * container, or an integer outside int64_t. */
static bool as_number(jb_json_tree_view_t value, number_t *out) {
  out->is_float = false;
  out->integer = 0;
  out->real = 0.0;
  if (as_integer(value, &out->integer))
    return true;
  jb_json_span_t token = {NULL, 0};
  if (jh_json_type(value) != JB_JSON_NUMBER ||
      jb_json_tree_text(value, &token) != HAL_OK)
    return false;
  bool integral = token.length != 0;
  for (size_t i = 0; integral && i < token.length; ++i)
    integral =
        token.data[i] == '-' || (token.data[i] >= '0' && token.data[i] <= '9');
  if (integral)
    return false; /* an int past int64_t */
  out->is_float = true;
  return jh_python_float(token, &out->real) == HAL_OK;
}

/* int == int or float, exactly as Python compares them. */
static bool equals_integer(number_t number, int64_t value) {
  if (!number.is_float)
    return number.integer == value;
  const double real = number.real;
  if (real != real || real < -9223372036854775808.0 ||
      real >= 9223372036854775808.0)
    return false;
  const int64_t whole = (int64_t)real;
  return (double)whole == real && whole == value;
}

/* a == b of two Python numbers. */
static bool numbers_equal(number_t a, number_t b) {
  if (!a.is_float && !b.is_float)
    return a.integer == b.integer;
  if (a.is_float && b.is_float)
    return a.real == b.real;
  return a.is_float ? equals_integer(a, b.integer)
                    : equals_integer(b, a.integer);
}

static number_t integer_number(int64_t value) {
  const number_t number = {false, value, 0.0};
  return number;
}

/* value == expected for a JSON value and an int: a number with the same
 * value, never a string or container. */
static bool json_equals(jb_json_tree_view_t value, int64_t expected) {
  number_t number = {false, 0, 0.0};
  return as_number(value, &number) && equals_integer(number, expected);
}

/* a // b of Python ints; false on a zero divisor or overflow. */
static bool floor_divide(int64_t a, int64_t b, int64_t *out) {
  if (b == 0 || (a == INT64_MIN && b == -1))
    return false;
  int64_t q = a / b;
  if (a % b != 0 && ((a < 0) != (b < 0)))
    --q;
  *out = q;
  return true;
}

static bool multiply(int64_t a, int64_t b, int64_t *out) {
  if (a == 0 || b == 0) {
    *out = 0;
    return true;
  }
  if ((a > 0 && b > 0 && a > INT64_MAX / b) ||
      (a < 0 && b < 0 && a < INT64_MAX / b) ||
      (a > 0 && b < 0 && b < INT64_MIN / a) ||
      (a < 0 && b > 0 && a < INT64_MIN / b))
    return false;
  *out = a * b;
  return true;
}

/* float(n): an int converted to the nearest binary64 value. */
static double real_of(number_t n) {
  return n.is_float ? n.real : (double)n.integer;
}

static number_t real_number(double value) {
  const number_t number = {true, 0, value};
  return number;
}

/* a // b of Python numbers: ints with floor division, otherwise floats as
 * float.__floordiv__ computes them (fmod, then floor rounded to nearest).
 * False for a zero divisor or an int quotient past int64_t. */
static bool python_floor_divide(number_t a, number_t b, number_t *out) {
  if (!a.is_float && !b.is_float) {
    int64_t q = 0;
    if (!floor_divide(a.integer, b.integer, &q))
      return false;
    *out = integer_number(q);
    return true;
  }
  const double vx = real_of(a), wx = real_of(b);
  if (wx == 0.0)
    return false;
  double mod = fmod(vx, wx);
  double div = (vx - mod) / wx;
  if (mod != 0.0) {
    if ((wx < 0) != (mod < 0))
      div -= 1.0;
  }
  double floordiv = 0.0;
  if (div != 0.0) {
    floordiv = floor(div);
    if (div - floordiv > 0.5)
      floordiv += 1.0;
  } else {
    floordiv = copysign(0.0, vx / wx);
  }
  *out = real_number(floordiv);
  return true;
}

/* a * b of Python numbers; false for an int product past int64_t. */
static bool python_multiply(number_t a, number_t b, number_t *out) {
  if (!a.is_float && !b.is_float) {
    int64_t product = 0;
    if (!multiply(a.integer, b.integer, &product))
      return false;
    *out = integer_number(product);
    return true;
  }
  *out = real_number(real_of(a) * real_of(b));
  return true;
}

/* ---- Trees ---- */

/* One frequency of the expected dict of check_tree. */
typedef struct {
  const char *name;
  number_t value;
} frequency_t;

typedef struct {
  jh_failure_t *failure;
  jb_json_span_t target, name; /* for "<target>/<name>" */
} tree_check_t;

static void put_where(jb_string_t *out, hal_status_t *status,
                      const tree_check_t *t) {
  jh_put_span(out, status, t->target);
  jh_put(out, status, "/");
  jh_put_span(out, status, t->name);
}

/* "<target>/<name>: <text>" as a ClockError. */
static hal_status_t fail_tree(const tree_check_t *t, const char *text) {
  jb_string_t message = {0};
  hal_status_t status = text_init(&message);
  put_where(&message, &status, t);
  jh_put(&message, &status, ": ");
  jh_put(&message, &status, text);
  status = fail_built(t->failure, status, &message);
  jb_string_destroy(&message);
  return status;
}

/* A parameter of the frequency formulas: a number, or a bool as Python
 * computes with it. A string, which Python cannot divide, is a ClockError
 * naming the value; an int past int64_t one about the overflow. */
static hal_status_t number_parameter(const tree_check_t *t,
                                     jb_json_tree_view_t params,
                                     const char *name, number_t *out) {
  jb_json_tree_view_t value = {0};
  if (!jh_json_member(params, name, &value)) {
    jb_string_t message = {0};
    hal_status_t status = text_init(&message);
    put_where(&message, &status, t);
    jh_put(&message, &status, ": missing parameters ['");
    jh_put(&message, &status, name);
    jh_put(&message, &status, "']");
    status = fail_built(t->failure, status, &message);
    jb_string_destroy(&message);
    return status;
  }
  if (as_number(value, out))
    return HAL_OK;
  if (jh_json_type(value) == JB_JSON_NUMBER) /* an int past int64_t */
    return fail_tree(t, "the parameters overflow 64-bit arithmetic");
  jb_string_t message = {0};
  hal_status_t status = text_init(&message);
  put_where(&message, &status, t);
  jh_put(&message, &status, ": parameter ");
  jh_put(&message, &status, name);
  jh_put(&message, &status, " is ");
  if (status == HAL_OK)
    status = jh_python_repr_json_append(&message, value);
  jh_put(&message, &status, ", the frequencies need a number");
  status = fail_built(t->failure, status, &message);
  jb_string_destroy(&message);
  return status;
}

static hal_status_t divide(const tree_check_t *t, number_t a, number_t b,
                           number_t *out) {
  return python_floor_divide(a, b, out)
             ? HAL_OK
             : fail_tree(t, "the parameters divide by zero or overflow 64-bit "
                            "arithmetic");
}

static hal_status_t times(const tree_check_t *t, number_t a, number_t b,
                          number_t *out) {
  return python_multiply(a, b, out)
             ? HAL_OK
             : fail_tree(t, "the parameters overflow 64-bit arithmetic");
}

/* requiredSources.<key>.frequencyHz, a number when the source is there. */
static bool source_frequency(jb_json_tree_view_t sources, const char *key,
                             number_t *out) {
  jb_json_tree_view_t source = {0}, frequency = {0};
  return jh_json_member(sources, key, &source) &&
         jh_json_member(source, "frequencyHz", &frequency) &&
         as_number(frequency, out);
}

static void put_number(jb_string_t *out, hal_status_t *status, number_t value) {
  if (value.is_float) {
    if (*status == HAL_OK)
      *status = jh_python_float_append(out, value.real);
  } else {
    jh_put_int64(out, status, value.integer);
  }
}

/* freq == expected: the same keys and equal numbers. */
static bool frequencies_match(jb_json_tree_view_t freq,
                              const frequency_t *expected, size_t count) {
  if (jh_json_count(freq) != count)
    return false;
  for (size_t i = 0; i < count; ++i) {
    jb_json_tree_view_t value = {0};
    number_t number = {false, 0, 0.0};
    if (!jh_json_member(freq, expected[i].name, &value) ||
        !as_number(value, &number) || !numbers_equal(number, expected[i].value))
      return false;
  }
  return true;
}

static hal_status_t check_frequencies(const tree_check_t *t,
                                      jb_json_tree_view_t freq,
                                      const frequency_t *expected,
                                      size_t count) {
  if (frequencies_match(freq, expected, count))
    return HAL_OK;
  jb_string_t message = {0};
  hal_status_t status = text_init(&message);
  put_where(&message, &status, t);
  jh_put(&message, &status, ": frequencies ");
  if (status == HAL_OK)
    status = jh_python_repr_json_append(&message, freq);
  jh_put(&message, &status, " do not follow from the parameters {");
  for (size_t i = 0; i < count; ++i) {
    jh_put(&message, &status, i == 0 ? "" : ", ");
    put_repr(&message, &status, jh_span(expected[i].name));
    jh_put(&message, &status, ": ");
    put_number(&message, &status, expected[i].value);
  }
  jh_put(&message, &status, "}");
  status = fail_built(t->failure, status, &message);
  jb_string_destroy(&message);
  return status;
}

/* vco // (div1 * div2) of the parameters names. */
static hal_status_t pll_output(const tree_check_t *t,
                               jb_json_tree_view_t params,
                               const char *const names[3], number_t *out) {
  number_t vco = {false, 0, 0.0};
  number_t div1 = {false, 0, 0.0};
  number_t div2 = {false, 0, 0.0};
  number_t divisor = {false, 0, 0.0};
  hal_status_t status = number_parameter(t, params, names[0], &vco);
  if (status == HAL_OK)
    status = number_parameter(t, params, names[1], &div1);
  if (status == HAL_OK)
    status = number_parameter(t, params, names[2], &div2);
  if (status == HAL_OK)
    status = times(t, div1, div2, &divisor);
  if (status == HAL_OK)
    status = divide(t, vco, divisor, out);
  return status;
}

static hal_status_t pico_frequencies(const tree_check_t *t,
                                     jb_json_tree_view_t params,
                                     jb_json_tree_view_t sources,
                                     frequency_t *expected) {
  number_t xosc = {false, 0, 0.0};
  if (!source_frequency(sources, "xosc", &xosc))
    return fail_tree(t, "needs the xosc crystal");
  static const char *const sys_names[3] = {"pllSysVcoHz", "pllSysPostdiv1",
                                           "pllSysPostdiv2"};
  static const char *const usb_names[3] = {"pllUsbVcoHz", "pllUsbPostdiv1",
                                           "pllUsbPostdiv2"};
  number_t sys_hz = integer_number(0), usb_hz = integer_number(0);
  hal_status_t status = pll_output(t, params, sys_names, &sys_hz);
  if (status == HAL_OK)
    status = pll_output(t, params, usb_names, &usb_hz);
  const frequency_t values[] = {{"ref", xosc},
                                {"sys", sys_hz},
                                {"peri", sys_hz},
                                {"usb", usb_hz},
                                {"adc", usb_hz}};
  memcpy(expected, values, sizeof values);
  return status;
}

static bool string_member(jb_json_tree_view_t object, const char *key,
                          const char *text) {
  jb_json_tree_view_t value = {0};
  jb_json_span_t got = {NULL, 0};
  return jh_json_member(object, key, &value) && jh_json_string(value, &got) &&
         jh_span_equal(got, jh_span(text));
}

static hal_status_t stm32_frequencies(const tree_check_t *t,
                                      jb_json_tree_view_t params,
                                      jb_json_tree_view_t sources,
                                      frequency_t *expected) {
  number_t source = integer_number(HSI16_HZ);
  if (!string_member(params, "pllSource", "hsi16") &&
      !source_frequency(sources, "hse", &source))
    return fail_tree(t, "the hse source is not required");
  number_t m = integer_number(0), n = integer_number(0);
  number_t r = integer_number(0), q = integer_number(0);
  number_t vco = integer_number(0), sysclk = integer_number(0);
  number_t fdcan = integer_number(0);
  hal_status_t status = number_parameter(t, params, "pllM", &m);
  if (status == HAL_OK)
    status = divide(t, source, m, &vco);
  if (status == HAL_OK)
    status = number_parameter(t, params, "pllN", &n);
  if (status == HAL_OK)
    status = times(t, vco, n, &vco);
  if (status == HAL_OK)
    status = number_parameter(t, params, "pllR", &r);
  if (status == HAL_OK)
    status = divide(t, vco, r, &sysclk);
  if (status == HAL_OK && string_member(params, "fdcanSource", "pll-q")) {
    /* Python stopped here with KeyError when pllQ was missing. */
    status = number_parameter(t, params, "pllQ", &q);
    if (status == HAL_OK)
      status = divide(t, vco, q, &fdcan);
  } else {
    fdcan = sysclk;
  }
  const frequency_t values[] = {
      {"sysclk", sysclk}, {"hclk", sysclk}, {"pclk1", sysclk},
      {"pclk2", sysclk},  {"fdcan", fdcan}, {"i2c", integer_number(HSI16_HZ)}};
  memcpy(expected, values, sizeof values);
  return status;
}

/* params[name] * 1000000 as Python computes it for a number or a bool; false
 * for another value or an int product past int64_t. */
static bool megahertz(jb_json_tree_view_t params, const char *name,
                      number_t *out) {
  jb_json_tree_view_t value = {0};
  number_t number = {false, 0, 0.0};
  return jh_json_member(params, name, &value) && as_number(value, &number) &&
         python_multiply(number, integer_number(1000000), out);
}

static hal_status_t esp_frequencies(const tree_check_t *t,
                                    jb_json_tree_view_t params,
                                    jb_json_tree_view_t sources,
                                    frequency_t *expected) {
  number_t xtal = {false, 0, 0.0}, wanted = {false, 0, 0.0};
  /* A string times 1000000 is a longer string in Python: never the
   * frequency. */
  if (!(source_frequency(sources, "xtal", &xtal) &&
        megahertz(params, "xtalFreqMhz", &wanted) &&
        numbers_equal(xtal, wanted)))
    return fail_tree(t, "the xtal source does not match xtalFreqMhz");
  expected[0].name = "cpu";
  if (megahertz(params, "cpuFreqMhz", &expected[0].value))
    return HAL_OK;
  jb_json_tree_view_t value = {0};
  number_t number = {false, 0, 0.0};
  if (jh_json_member(params, "cpuFreqMhz", &value) && as_number(value, &number))
    return fail_tree(t, "the parameters overflow 64-bit arithmetic");
  return number_parameter(t, params, "cpuFreqMhz", &number);
}

/* check_tree: the output frequencies follow from the backend parameters. */
static hal_status_t check_tree(jh_failure_t *failure, jb_json_span_t target,
                               jb_json_span_t name, jb_json_tree_view_t tree) {
  const tree_check_t t = {failure, target, name};
  jb_json_tree_view_t value = {0};
  jb_json_span_t backend_name = {NULL, 0};
  (void)jh_json_member(tree, "backend", &value);
  (void)jh_json_string(value, &backend_name);
  const backend_t *backend = find_backend(backend_name);
  jb_string_t message = {0};
  hal_status_t status = text_init(&message);
  if (status == HAL_OK && !backend_drives(backend, target)) {
    put_where(&message, &status, &t);
    jh_put(&message, &status, ": backend ");
    jh_put_span(&message, &status, backend_name);
    jh_put(&message, &status, " does not drive ");
    jh_put_span(&message, &status, target);
    status = fail_built(failure, status, &message);
  }
  if (status == HAL_OK && backend->default_only &&
      !jh_span_equal(name, jh_span("default"))) {
    put_where(&message, &status, &t);
    jh_put(&message, &status, ": the HAL does not set the ");
    jh_put_span(&message, &status, backend_name);
    jh_put(&message, &status, " clock, so only its default tree runs");
    status = fail_built(failure, status, &message);
  }
  jb_json_tree_view_t params = {0}, freq = {0}, sources = {0};
  (void)jh_json_member(tree, "parameters", &params);
  (void)jh_json_member(tree, "frequenciesHz", &freq);
  (void)jh_json_member(tree, "requiredSources", &sources);
  jb_vector_t missing = {0};
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jh_list_init(&missing));
  for (size_t i = 0; status == HAL_OK && i < backend->parameter_count; ++i)
    if (!jh_json_member(params, backend->parameters[i], &value))
      status = jh_fail_storage(
          failure, jh_list_push(&missing, jh_span(backend->parameters[i])));
  if (status == HAL_OK && jb_vector_size(&missing) != 0) {
    jh_list_sort(&missing);
    jb_string_clear(&message);
    put_where(&message, &status, &t);
    jh_put(&message, &status, ": missing parameters ");
    if (status == HAL_OK)
      status = jh_repr_list(&message, &missing);
    status = fail_built(failure, status, &message);
  }
  jh_list_destroy(&missing);
  frequency_t expected[6];
  size_t count = 0;
  if (status == HAL_OK) {
    switch (backend->kind) {
    case PICO_SDK:
      status = pico_frequencies(&t, params, sources, expected);
      count = 5;
      break;
    case STM32G474:
      status = stm32_frequencies(&t, params, sources, expected);
      count = 6;
      break;
    case ESP_IDF:
      status = esp_frequencies(&t, params, sources, expected);
      count = 1;
      break;
    case MOCK:
      if (jh_json_count(sources) != 0 || jh_json_count(params) != 0)
        status = fail_tree(&t, "the mock has no clock");
      break;
    }
  }
  if (status == HAL_OK)
    status = check_frequencies(&t, freq, expected, count);
  jb_string_destroy(&message);
  return status;
}

/* ---- Registry ---- */

void jh_clock_registry_init(jh_clock_registry_t *registry) {
  memset(registry, 0, sizeof *registry);
  (void)jb_vector_init(&registry->files, sizeof(jh_clock_file_t),
                       jb_heap_allocator());
}

static void file_destroy(jh_clock_file_t *file) {
  jb_json_tree_destroy(file->tree);
  jb_string_destroy(&file->target);
  jb_string_destroy(&file->path);
  memset(file, 0, sizeof *file);
}

void jh_clock_registry_destroy(jh_clock_registry_t *registry) {
  for (size_t i = 0; i < jb_vector_size(&registry->files); ++i)
    file_destroy(jb_vector_at(&registry->files, i));
  jb_vector_destroy(&registry->files);
  memset(registry, 0, sizeof *registry);
}

static const jh_clock_file_t *file_at(const jh_clock_registry_t *registry,
                                      size_t index) {
  return jb_vector_const_at(&registry->files, index);
}

const jh_clock_file_t *
jh_clock_registry_find(const jh_clock_registry_t *registry,
                       jb_json_span_t target) {
  for (size_t i = 0; i < jb_vector_size(&registry->files); ++i)
    if (jh_span_equal(jh_string_span(&file_at(registry, i)->target), target))
      return file_at(registry, i);
  return NULL;
}

static jb_json_tree_view_t trees_of(const jh_clock_file_t *file) {
  jb_json_tree_view_t trees = {0};
  (void)jh_json_member(file->root, "trees", &trees);
  return trees;
}

size_t jh_clock_registry_tree_count(const jh_clock_registry_t *registry) {
  size_t count = 0;
  for (size_t i = 0; i < jb_vector_size(&registry->files); ++i)
    count += jh_json_count(trees_of(file_at(registry, i)));
  return count;
}

/* hardware_schema.pointer_key: tokens of digits by number before other
 * tokens in code point order, a shorter pointer first. */
static int pointer_compare(jb_json_span_t a, jb_json_span_t b) {
  size_t at_a = 0, at_b = 0;
  while (at_a < a.length && at_b < b.length) {
    size_t end_a = at_a + 1, end_b = at_b + 1;
    while (end_a < a.length && a.data[end_a] != '/')
      ++end_a;
    while (end_b < b.length && b.data[end_b] != '/')
      ++end_b;
    const jb_json_span_t ta = {a.data + at_a + 1, end_a - at_a - 1};
    const jb_json_span_t tb = {b.data + at_b + 1, end_b - at_b - 1};
    bool da = ta.length != 0, db = tb.length != 0;
    for (size_t i = 0; da && i < ta.length; ++i)
      da = ta.data[i] >= '0' && ta.data[i] <= '9';
    for (size_t i = 0; db && i < tb.length; ++i)
      db = tb.data[i] >= '0' && tb.data[i] <= '9';
    int order = 0;
    if (da != db) {
      order = da ? -1 : 1;
    } else if (da) {
      /* int(token): compare without leading zeros, then by length. */
      size_t za = 0, zb = 0;
      while (za + 1 < ta.length && ta.data[za] == '0')
        ++za;
      while (zb + 1 < tb.length && tb.data[zb] == '0')
        ++zb;
      const jb_json_span_t na = {ta.data + za, ta.length - za};
      const jb_json_span_t nb = {tb.data + zb, tb.length - zb};
      order = na.length != nb.length ? (na.length < nb.length ? -1 : 1)
                                     : jh_span_compare(na, nb);
    } else {
      order = jh_span_compare(ta, tb);
    }
    if (order != 0)
      return order;
    at_a = end_a;
    at_b = end_b;
  }
  return at_a < a.length ? 1 : (at_b < b.length ? -1 : 0);
}

/* "[JH-HW-SCHEMA] <path>:<pointer>: <message>" of the failure with the
 * smallest pointer, as SchemaSet.first_error picks it. */
static hal_status_t fail_schema(jh_failure_t *failure, jb_json_span_t path,
                                const jh_schema_files_t *files) {
  const jh_schema_report_t *first = jh_schema_files_report(files, 0);
  for (size_t i = 1; i < jh_schema_files_report_count(files); ++i) {
    const jh_schema_report_t *report = jh_schema_files_report(files, i);
    if (pointer_compare(jh_string_span(&report->pointer),
                        jh_string_span(&first->pointer)) < 0)
      first = report;
  }
  jb_string_t message = {0};
  hal_status_t status = text_init(&message);
  jh_put(&message, &status, "[JH-HW-SCHEMA] ");
  jh_put_span(&message, &status, path);
  jh_put(&message, &status, ":");
  if (first != NULL) {
    jh_put_span(&message, &status, jh_string_span(&first->pointer));
    jh_put(&message, &status, ": ");
    jh_put_span(&message, &status, jh_string_span(&first->message));
  } else {
    jh_put(&message, &status, ": the clock tree schema rejects this file");
  }
  status = fail_built(failure, status, &message);
  jb_string_destroy(&message);
  return status;
}

/* Read path with the hardware loader: "<path>: invalid JSON: <reason>" (exit
 * code 2) or the file failure (exit code 3) prefixed with the path. */
static hal_status_t read_file(jh_json_location_t *where, const char *path,
                              size_t limit, jh_json_document_t *document,
                              jh_failure_t *failure) {
  jh_failure_t reason;
  jh_failure_init(&reason);
  hal_status_t status =
      jh_json_read_document(where, path, limit, true, document, &reason);
  if (status != HAL_OK) {
    if (reason.exit_code == 2)
      status = jh_check_json_error(failure, jh_span(path), where, &reason);
    else if (reason.exit_code == 4)
      status = jh_fail_storage(failure, reason.status);
    else
      status = jh_fail(failure, reason.status, reason.exit_code,
                       JH_PARTS(jh_span(path), jh_span(": "),
                                jh_string_span(&reason.message)));
  }
  jh_failure_destroy(&reason);
  return status;
}

static int file_order(const void *left, const void *right) {
  const jh_clock_file_t *a = left, *b = right;
  return jh_span_compare(jh_string_span(&a->target),
                         jh_string_span(&b->target));
}

hal_status_t jh_clock_registry_load(jh_clock_registry_t *registry,
                                    const char *checkout_root,
                                    const char *clocks_dir, size_t limit,
                                    jh_failure_t *failure) {
  jb_vector_t names = {0};
  jb_string_t path = {0}, schema = {0};
  jh_json_location_t where, schema_where;
  jh_json_location_init(&where);
  jh_json_location_init(&schema_where);
  jh_schema_files_t files;
  jh_schema_files_init(&files, &schema_where, limit);
  hal_status_t status = jh_fail_storage(failure, jh_list_init(&names));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&path));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&schema));
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure,
        jh_path_join(checkout_root, "config/hardware/clock_tree.schema.json",
                     &schema));
  if (status == HAL_OK)
    status =
        jh_schema_files_root(&files, jb_string_data(&schema), NULL, failure);
  if (status == HAL_OK) {
    const hal_status_t listed = jh_json_names(clocks_dir, &names);
    if (listed == HAL_ENOMEM || listed == HAL_EOVERFLOW)
      status = jh_fail_storage(failure, listed);
    else if (listed != HAL_OK)
      status = jh_fail(failure, listed, 3,
                       JH_PARTS(jh_span(clocks_dir),
                                jh_span(": cannot list the clock registry: "),
                                jh_span(hal_status_to_string(listed))));
  }
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&names); ++i) {
    status = jh_fail_storage(
        failure,
        jh_path_join(clocks_dir, jb_string_data(jh_list_at(&names, i)), &path));
    jh_json_document_t document = {0};
    if (status == HAL_OK)
      status =
          read_file(&where, jb_string_data(&path), limit, &document, failure);
    if (status == HAL_OK) {
      const hal_status_t valid =
          jh_schema_files_validate(&files, &document, failure);
      if (valid == HAL_ECONFIG)
        status = fail_schema(failure, jh_string_span(&path), &files);
      else
        status = valid;
    }
    jh_clock_file_t file = {0};
    if (status == HAL_OK)
      status = jh_fail_storage(failure, text_init(&file.target));
    if (status == HAL_OK)
      status = jh_fail_storage(failure, text_init(&file.path));
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure, jb_string_assign(&file.path, jb_string_data(&path),
                                    jb_string_size(&path)));
    jb_json_node_t root = {0};
    if (status == HAL_OK && jb_json_root(&document.doc, &root) != HAL_OK)
      status = jh_fail_text(failure, HAL_EINTERNAL, 4, "document has no root");
    if (status == HAL_OK) {
      const jb_json_tree_limits_t limits = {1000000, 256, limit,
                                            limit * 32 + 1048576};
      status = jh_fail_storage(failure,
                               jb_json_tree_create(root, &limits, &file.tree));
    }
    if (status == HAL_OK)
      status =
          jh_fail_storage(failure, jb_json_tree_root(file.tree, &file.root));
    jb_json_tree_view_t target = {0};
    jb_json_span_t target_id = {NULL, 0};
    (void)jh_json_member(file.root, "target", &target);
    (void)jh_json_string(target, &target_id);
    if (status == HAL_OK)
      status = jh_fail_storage(
          failure,
          jb_string_assign(&file.target, target_id.data, target_id.length));
    if (status == HAL_OK &&
        !jh_span_equal(jh_span_path_stem(jh_string_span(&path)), target_id))
      status = fail_check(failure, JH_PARTS(jh_string_span(&path),
                                            jh_span(": file name must be "),
                                            target_id, jh_span(".json")));
    if (status == HAL_OK)
      status = jh_fail_storage(failure,
                               jb_vector_append(&registry->files, &file, 1));
    if (status != HAL_OK)
      file_destroy(&file);
    jh_json_document_destroy(&document);
  }
  if (status == HAL_OK && jb_vector_size(&registry->files) > 1)
    qsort(jb_vector_data(&registry->files), jb_vector_size(&registry->files),
          sizeof(jh_clock_file_t), file_order);
  jh_schema_files_destroy(&files);
  jh_json_location_destroy(&schema_where);
  jh_json_location_destroy(&where);
  jb_string_destroy(&schema);
  jb_string_destroy(&path);
  jh_list_destroy(&names);
  return status;
}

/* ---- Registry against targets and boards ---- */

static jb_json_tree_view_t clock_inputs(const jh_board_descriptor_t *target) {
  jb_json_tree_view_t inputs = {0};
  (void)jh_json_member(target->root, "clockInputs", &inputs);
  return inputs;
}

/* The members of an object in code point order of their names, as sorted()
 * of dict.items() visits them: indices into the object. */
static hal_status_t sorted_members(jb_json_tree_view_t object,
                                   jb_vector_t *order) {
  hal_status_t status =
      jb_vector_init(order, sizeof(size_t), jb_heap_allocator());
  for (size_t i = 0; status == HAL_OK && i < jh_json_count(object); ++i) {
    size_t at = jb_vector_size(order);
    const jb_json_span_t key = jh_json_key(jh_json_item(object, i));
    while (at > 0 &&
           jh_span_compare(
               jh_json_key(jh_json_item(
                   object, *(const size_t *)jb_vector_const_at(order, at - 1))),
               key) > 0)
      --at;
    status = jb_vector_insert(order, at, &i, 1);
  }
  return status;
}

static jb_json_tree_view_t member_in_order(jb_json_tree_view_t object,
                                           const jb_vector_t *order,
                                           size_t index) {
  return jh_json_item(object,
                      *(const size_t *)jb_vector_const_at(order, index));
}

/* "clock registries [...] differ from targets [...]" unless both hold the
 * same IDs. */
static hal_status_t check_targets(const jh_clock_registry_t *registry,
                                  const jh_board_registry_t *boards,
                                  jh_failure_t *failure) {
  jb_vector_t have = {0}, want = {0};
  hal_status_t status = jh_fail_storage(failure, jh_list_init(&have));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jh_list_init(&want));
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&registry->files);
       ++i)
    status = jh_fail_storage(
        failure,
        jh_list_push(&have, jh_string_span(&file_at(registry, i)->target)));
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&boards->targets);
       ++i) {
    const jh_board_descriptor_t *target =
        jb_vector_const_at(&boards->targets, i);
    status = jh_fail_storage(failure,
                             jh_list_push(&want, jh_string_span(&target->id)));
  }
  jh_list_sort(&have);
  jh_list_sort(&want);
  bool same = jb_vector_size(&have) == jb_vector_size(&want);
  for (size_t i = 0; same && i < jb_vector_size(&have); ++i)
    same = jh_span_equal(jh_string_span(jh_list_at(&have, i)),
                         jh_string_span(jh_list_at(&want, i)));
  if (status == HAL_OK && !same) {
    jb_string_t message = {0};
    status = text_init(&message);
    jh_put(&message, &status, "clock registries ");
    if (status == HAL_OK)
      status = jh_repr_list(&message, &have);
    jh_put(&message, &status, " differ from targets ");
    if (status == HAL_OK)
      status = jh_repr_list(&message, &want);
    status = fail_built(failure, status, &message);
    jb_string_destroy(&message);
  }
  jh_list_destroy(&want);
  jh_list_destroy(&have);
  return status;
}

hal_status_t jh_clock_check_registry(const jh_clock_registry_t *registry,
                                     const jh_board_registry_t *boards,
                                     jh_failure_t *failure) {
  hal_status_t status = check_targets(registry, boards, failure);
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&registry->files);
       ++i) {
    const jh_clock_file_t *file = file_at(registry, i);
    const jb_json_span_t target = jh_string_span(&file->target);
    const jb_json_tree_view_t inputs =
        clock_inputs(jh_board_registry_find(&boards->targets, target));
    const jb_json_tree_view_t trees = trees_of(file);
    jb_vector_t order = {0};
    status = jh_fail_storage(failure, sorted_members(trees, &order));
    for (size_t t = 0; status == HAL_OK && t < jb_vector_size(&order); ++t) {
      const jb_json_tree_view_t tree = member_in_order(trees, &order, t);
      const jb_json_span_t name = jh_json_key(tree);
      status = check_tree(failure, target, name, tree);
      jb_json_tree_view_t sources = {0};
      (void)jh_json_member(tree, "requiredSources", &sources);
      jb_vector_t keys = {0};
      if (status == HAL_OK)
        status = jh_fail_storage(failure, sorted_members(sources, &keys));
      for (size_t k = 0; status == HAL_OK && k < jb_vector_size(&keys); ++k) {
        const jb_json_tree_view_t source = member_in_order(sources, &keys, k);
        const jb_json_span_t key = jh_json_key(source);
        jb_json_tree_view_t kind = {0}, kinds = {0}, found = {0};
        jb_json_span_t kind_text = {NULL, 0};
        (void)jh_json_member(source, "kind", &kind);
        (void)jh_json_string(kind, &kind_text);
        const bool known =
            jh_json_type(inputs) == JB_JSON_OBJECT &&
            jb_json_tree_find(inputs, key, &kinds) == HAL_OK &&
            jh_json_type(kinds) == JB_JSON_OBJECT &&
            jb_json_tree_find(kinds, kind_text, &found) == HAL_OK;
        if (!known)
          status = fail_check(
              failure, JH_PARTS(target, jh_span("/"), name, jh_span(": "),
                                target, jh_span(" has no "), key,
                                jh_span(" input for the "), kind_text));
      }
      jb_vector_destroy(&keys);
    }
    jb_vector_destroy(&order);
  }
  return status;
}

hal_status_t jh_clock_source_errors_init(jb_vector_t *errors) {
  return jb_vector_init(errors, sizeof(jh_clock_source_error_t),
                        jb_heap_allocator());
}

void jh_clock_source_errors_destroy(jb_vector_t *errors) {
  for (size_t i = 0; i < jb_vector_size(errors); ++i) {
    jh_clock_source_error_t *error = jb_vector_at(errors, i);
    jb_string_destroy(&error->pointer);
    jb_string_destroy(&error->message);
  }
  jb_vector_destroy(errors);
}

static hal_status_t push_error(jb_vector_t *errors, const jb_string_t *pointer,
                               const jb_string_t *message) {
  jh_clock_source_error_t error = {{0}, {0}};
  hal_status_t status = text_init(&error.pointer);
  if (status == HAL_OK)
    status = text_init(&error.message);
  if (status == HAL_OK)
    status = jh_append_span(&error.pointer, jh_string_span(pointer));
  if (status == HAL_OK)
    status = jh_append_span(&error.message, jh_string_span(message));
  if (status == HAL_OK)
    status = jb_vector_append(errors, &error, 1);
  if (status != HAL_OK) {
    jb_string_destroy(&error.pointer);
    jb_string_destroy(&error.message);
  }
  return status;
}

hal_status_t jh_clock_source_errors(jb_json_tree_view_t sources,
                                    jb_json_tree_view_t tree,
                                    jb_json_tree_view_t inputs,
                                    jb_vector_t *errors,
                                    jh_failure_t *failure) {
  jb_json_tree_view_t required = {0};
  (void)jh_json_member(tree, "requiredSources", &required);
  jb_vector_t keys = {0};
  jb_string_t pointer = {0}, message = {0};
  hal_status_t status = sorted_members(required, &keys);
  if (status == HAL_OK)
    status = text_init(&pointer);
  if (status == HAL_OK)
    status = text_init(&message);
  for (size_t k = 0; status == HAL_OK && k < jb_vector_size(&keys); ++k) {
    const jb_json_tree_view_t want = member_in_order(required, &keys, k);
    const jb_json_span_t key = jh_json_key(want);
    jb_json_tree_view_t source = {0};
    const bool present = jh_json_type(sources) == JB_JSON_OBJECT &&
                         jb_json_tree_find(sources, key, &source) == HAL_OK &&
                         jh_json_type(source) != JB_JSON_NULL;
    if (!present) {
      status = jh_join(&pointer, JH_PARTS(jh_span("/"), key));
      if (status == HAL_OK)
        status = jh_join(&message,
                         JH_PARTS(jh_span("no "), key, jh_span(" source")));
      if (status == HAL_OK)
        status = push_error(errors, &pointer, &message);
      continue;
    }
    static const char *const fields[] = {"kind", "frequencyHz"};
    bool differs = false;
    for (size_t f = 0; status == HAL_OK && !differs && f < COUNTOF(fields);
         ++f) {
      jb_json_tree_view_t have = {0}, wanted = {0};
      (void)jh_json_member(source, fields[f], &have);
      (void)jh_json_member(want, fields[f], &wanted);
      if (f == 0) {
        jb_json_span_t a = {NULL, 0}, b = {NULL, 0};
        differs = !(jh_json_string(have, &a) && jh_json_string(wanted, &b) &&
                    jh_span_equal(a, b));
      } else {
        number_t a = {false, 0, 0.0}, b = {false, 0, 0.0};
        differs = !(as_number(have, &a) && as_number(wanted, &b) &&
                    numbers_equal(a, b));
      }
      if (!differs)
        continue;
      status = jh_join(&pointer, JH_PARTS(jh_span("/"), key, jh_span("/"),
                                          jh_span(fields[f])));
      jb_string_clear(&message);
      jh_put_span(&message, &status, key);
      jh_put(&message, &status, " is a ");
      jb_json_tree_view_t value = {0};
      (void)jh_json_member(source, "kind", &value);
      put_str(&message, &status, value);
      jh_put(&message, &status, " at ");
      (void)jh_json_member(source, "frequencyHz", &value);
      put_str(&message, &status, value);
      jh_put(&message, &status, " Hz, the tree needs a ");
      (void)jh_json_member(want, "kind", &value);
      put_str(&message, &status, value);
      jh_put(&message, &status, " at ");
      (void)jh_json_member(want, "frequencyHz", &value);
      put_str(&message, &status, value);
      jh_put(&message, &status, " Hz");
      if (status == HAL_OK)
        status = push_error(errors, &pointer, &message);
    }
    if (status == HAL_OK && !differs) {
      status = jh_board_clock_source_problem(inputs, key, source, &message);
      if (status == HAL_OK && jb_string_size(&message) != 0) {
        status =
            jh_join(&pointer, JH_PARTS(jh_span("/"), key, jh_span("/pins")));
        if (status == HAL_OK)
          status = push_error(errors, &pointer, &message);
      }
    }
  }
  jb_string_destroy(&message);
  jb_string_destroy(&pointer);
  jb_vector_destroy(&keys);
  return jh_fail_storage(failure, status);
}

static int board_order(const void *left, const void *right) {
  const jh_board_descriptor_t *const *a = left, *const *b = right;
  return jh_span_compare(jh_string_span(&(*a)->id), jh_string_span(&(*b)->id));
}

hal_status_t jh_clock_check_boards(const jh_clock_registry_t *registry,
                                   const jh_board_registry_t *boards,
                                   jh_failure_t *failure) {
  /* sorted(boards.items()): by board ID, which differs from the file name
   * order for IDs such as pico and pico-w. */
  jb_vector_t order = {0};
  hal_status_t status = jh_fail_storage(
      failure, jb_vector_init(&order, sizeof(const jh_board_descriptor_t *),
                              jb_heap_allocator()));
  for (size_t b = 0; status == HAL_OK && b < jb_vector_size(&boards->boards);
       ++b) {
    const jh_board_descriptor_t *board = jb_vector_const_at(&boards->boards, b);
    status = jh_fail_storage(failure, jb_vector_append(&order, &board, 1));
  }
  if (status == HAL_OK && jb_vector_size(&order) > 1)
    qsort(jb_vector_data(&order), jb_vector_size(&order),
          sizeof(const jh_board_descriptor_t *), board_order);
  for (size_t b = 0; status == HAL_OK && b < jb_vector_size(&order); ++b) {
    const jh_board_descriptor_t *board =
        *(const jh_board_descriptor_t *const *)jb_vector_const_at(&order, b);
    jb_json_tree_view_t compatible = {0}, sources = {0};
    (void)jh_json_member(board->root, "compatibleTargets", &compatible);
    (void)jh_json_member(board->root, "clockSources", &sources);
    for (size_t t = 0; status == HAL_OK && t < jh_json_count(compatible); ++t) {
      jb_json_span_t target = {NULL, 0};
      (void)jh_json_string(jh_json_item(compatible, t), &target);
      const jh_clock_file_t *file = jh_clock_registry_find(registry, target);
      jb_json_tree_view_t tree = {0};
      if (file == NULL || !jh_json_member(trees_of(file), "default", &tree)) {
        status = fail_check(
            failure, JH_PARTS(jh_span("no clock registry for "), target));
        break;
      }
      jb_vector_t errors = {0};
      status = jh_fail_storage(failure, jh_clock_source_errors_init(&errors));
      if (status == HAL_OK)
        status = jh_clock_source_errors(
            sources, tree,
            clock_inputs(jh_board_registry_find(&boards->targets, target)),
            &errors, failure);
      if (status == HAL_OK && jb_vector_size(&errors) != 0) {
        jb_string_t message = {0};
        status = text_init(&message);
        jh_put_span(&message, &status, jh_string_span(&board->id));
        jh_put(&message, &status, ": default ");
        jh_put_span(&message, &status, target);
        jh_put(&message, &status, " tree: ");
        for (size_t e = 0; e < jb_vector_size(&errors); ++e) {
          const jh_clock_source_error_t *error = jb_vector_const_at(&errors, e);
          jh_put(&message, &status, e == 0 ? "" : "; ");
          jh_put_span(&message, &status, jh_string_span(&error->message));
        }
        status = fail_built(failure, status, &message);
        jb_string_destroy(&message);
      }
      jh_clock_source_errors_destroy(&errors);
    }
  }
  jb_vector_destroy(&order);
  return status;
}

/* The descriptors of directory: ids (file stems) and trees, as json.loads
 * reads them. */
typedef struct {
  jb_vector_t ids;   /* jb_string_t */
  jb_vector_t trees; /* jb_json_tree_t * */
} raw_descriptors_t;

static void raw_destroy(raw_descriptors_t *raw) {
  for (size_t i = 0; i < jb_vector_size(&raw->trees); ++i)
    jb_json_tree_destroy(*(jb_json_tree_t **)jb_vector_at(&raw->trees, i));
  jb_vector_destroy(&raw->trees);
  jh_list_destroy(&raw->ids);
}

static hal_status_t raw_load(const char *directory, size_t limit,
                             raw_descriptors_t *raw, jh_failure_t *failure) {
  jb_vector_t names = {0};
  jb_string_t path = {0};
  hal_status_t status = jh_fail_storage(failure, jh_list_init(&raw->ids));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jb_vector_init(&raw->trees,
                                                     sizeof(jb_json_tree_t *),
                                                     jb_heap_allocator()));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jh_list_init(&names));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&path));
  if (status == HAL_OK) {
    const hal_status_t listed = jh_json_names(directory, &names);
    if (listed != HAL_OK)
      status = jh_fail(failure, listed, listed == HAL_ENOMEM ? 4 : 3,
                       JH_PARTS(jh_span(directory), jh_span(": cannot list: "),
                                jh_span(hal_status_to_string(listed))));
  }
  for (size_t i = 0; status == HAL_OK && i < jb_vector_size(&names); ++i) {
    status = jh_fail_storage(
        failure,
        jh_path_join(directory, jb_string_data(jh_list_at(&names, i)), &path));
    jb_json_tree_t *tree = NULL;
    jb_json_tree_view_t root = {0};
    if (status == HAL_OK)
      status = jh_check_load_object(jb_string_data(&path), limit, &tree, &root,
                                    failure);
    if (status == HAL_OK)
      status =
          jh_fail_storage(failure, jb_vector_append(&raw->trees, &tree, 1));
    if (status != HAL_OK) {
      jb_json_tree_destroy(tree);
      break;
    }
    status = jh_fail_storage(
        failure,
        jh_list_push(&raw->ids, jh_span_path_stem(jh_string_span(&path))));
  }
  jb_string_destroy(&path);
  jh_list_destroy(&names);
  return status;
}

static jb_json_tree_view_t raw_root(const raw_descriptors_t *raw,
                                    size_t index) {
  jb_json_tree_view_t root = {0};
  (void)jb_json_tree_root(
      *(jb_json_tree_t *const *)jb_vector_const_at(&raw->trees, index), &root);
  return root;
}

hal_status_t jh_clock_board_sources(const jh_clock_registry_t *registry,
                                    const char *boards_root, size_t limit,
                                    jh_clock_board_sources_fn fn, void *user,
                                    jh_failure_t *failure) {
  raw_descriptors_t targets = {{0}, {0}}, boards = {{0}, {0}};
  jb_string_t directory = {0};
  hal_status_t status = jh_fail_storage(failure, text_init(&directory));
  if (status == HAL_OK)
    status = jh_fail_storage(failure,
                             jh_path_join(boards_root, "targets", &directory));
  if (status == HAL_OK)
    status = raw_load(jb_string_data(&directory), limit, &targets, failure);
  if (status == HAL_OK)
    status = jh_fail_storage(failure,
                             jh_path_join(boards_root, "profiles", &directory));
  if (status == HAL_OK)
    status = raw_load(jb_string_data(&directory), limit, &boards, failure);
  for (size_t b = 0; status == HAL_OK && b < jb_vector_size(&boards.ids); ++b) {
    const jb_json_tree_view_t board = raw_root(&boards, b);
    jb_json_tree_view_t compatible = {0}, sources = {0};
    (void)jh_json_member(board, "compatibleTargets", &compatible);
    (void)jh_json_member(board, "clockSources", &sources);
    for (size_t c = 0; status == HAL_OK && c < jh_json_count(compatible); ++c) {
      jb_json_span_t target_id = {NULL, 0};
      if (!jh_json_string(jh_json_item(compatible, c), &target_id))
        continue;
      const jh_clock_file_t *file = jh_clock_registry_find(registry, target_id);
      jb_json_tree_view_t inputs = {0};
      bool known = false;
      for (size_t t = 0; !known && t < jb_vector_size(&targets.ids); ++t)
        if (jh_span_equal(jh_string_span(jh_list_at(&targets.ids, t)),
                          target_id)) {
          known = true;
          (void)jh_json_member(raw_root(&targets, t), "clockInputs", &inputs);
        }
      if (file == NULL || !known)
        continue;
      const jb_json_tree_view_t trees = trees_of(file);
      for (size_t n = 0; status == HAL_OK && n < jh_json_count(trees); ++n) {
        const jb_json_tree_view_t tree = jh_json_item(trees, n);
        jb_vector_t errors = {0};
        status = jh_fail_storage(failure, jh_clock_source_errors_init(&errors));
        if (status == HAL_OK)
          status =
              jh_clock_source_errors(sources, tree, inputs, &errors, failure);
        if (status == HAL_OK)
          status = fn(user, jh_string_span(jh_list_at(&boards.ids, b)),
                      target_id, jh_json_key(tree), &errors);
        jh_clock_source_errors_destroy(&errors);
      }
    }
  }
  raw_destroy(&boards);
  raw_destroy(&targets);
  jb_string_destroy(&directory);
  return status;
}

/* ---- Text of the code that sets the clocks ---- */

/* A file as Path.read_text() returns it: universal newlines, so CR LF and a
 * lone CR read as LF. */
static hal_status_t read_text(const char *path, size_t limit, jb_string_t *out,
                              jh_failure_t *failure) {
  jb_file_data_t data = {0};
  hal_status_t status = jb_file_read(path, limit, &data);
  if (status == HAL_ENOMEM)
    return jh_fail_storage(failure, status);
  if (status != HAL_OK)
    return jh_fail(failure, status, 3,
                   JH_PARTS(jh_span(path), jh_span(": cannot read: "),
                            jh_span(hal_status_to_string(status))));
  jb_string_clear(out);
  status = jb_string_reserve(out, data.length);
  for (size_t i = 0; status == HAL_OK && i < data.length; ++i) {
    char ch = (char)data.data[i];
    if (ch == '\r') {
      ch = '\n';
      if (i + 1 < data.length && data.data[i + 1] == '\n')
        ++i;
    }
    status = jb_string_append(out, &ch, 1);
  }
  jb_file_release(&data);
  return jh_fail_storage(failure, status);
}

/* One compiled pattern of the Python re module: PCRE2 on bytes, where the
 * \s, \w, \b and \d of a pattern know ASCII only. */
typedef struct {
  pcre2_code *code;
  pcre2_match_data *match;
} pattern_t;

static hal_status_t compile(pattern_t *pattern, const char *text,
                            jh_failure_t *failure) {
  int error = 0;
  PCRE2_SIZE offset = 0;
  pattern->match = NULL;
  pattern->code = pcre2_compile((PCRE2_SPTR)text, PCRE2_ZERO_TERMINATED, 0,
                                &error, &offset, NULL);
  if (pattern->code != NULL)
    pattern->match = pcre2_match_data_create_from_pattern(pattern->code, NULL);
  if (pattern->match != NULL)
    return HAL_OK;
  pcre2_code_free(pattern->code);
  pattern->code = NULL;
  return error == PCRE2_ERROR_NOMEMORY || pattern->code == NULL
             ? jh_fail_text(failure, HAL_ENOMEM, 4,
                            "cannot compile a clock registry pattern")
             : jh_fail_storage(failure, HAL_ENOMEM);
}

static void release(pattern_t *pattern) {
  pcre2_match_data_free(pattern->match);
  pcre2_code_free(pattern->code);
  pattern->match = NULL;
  pattern->code = NULL;
}

/* A matcher failure (a step limit, memory) for the text of where; it never
 * reads as no match. */
static hal_status_t search_failed(int code, jb_json_span_t where,
                                  jh_failure_t *failure) {
  PCRE2_UCHAR reason[256];
  if (pcre2_get_error_message(code, reason, sizeof reason) < 0)
    memcpy(reason, "PCRE2 error", sizeof "PCRE2 error");
  return jh_fail(failure,
                 code == PCRE2_ERROR_NOMEMORY ? HAL_ENOMEM : HAL_EOVERFLOW, 4,
                 JH_PARTS(where, jh_span(": the pattern search stopped: "),
                          jh_span((const char *)reason)));
}

/* Steps the matcher may take on text: Python's re has no limit, so one
 * backtracking pass over all of a large file must fit (64 steps per byte, at
 * least PCRE2's default of ten million); a search that needs more fails. */
static uint32_t match_limit(size_t length) {
  const uint64_t steps = (uint64_t)length * 64u;
  return steps < 10000000u    ? 10000000u
         : steps > UINT32_MAX ? UINT32_MAX
                              : (uint32_t)steps;
}

/* re.search from start: *found and the bounds of group in *begin and *end. */
static hal_status_t search(const pattern_t *pattern, jb_json_span_t text,
                           size_t start, uint32_t group, size_t *begin,
                           size_t *end, bool *found, jb_json_span_t where,
                           jh_failure_t *failure) {
  *found = false;
  pcre2_match_context *context = pcre2_match_context_create(NULL);
  if (context == NULL)
    return jh_fail_storage(failure, HAL_ENOMEM);
  const uint32_t limit = match_limit(text.length);
  (void)pcre2_set_match_limit(context, limit);
  (void)pcre2_set_depth_limit(context, limit);
  const int matched =
      pcre2_match(pattern->code, (PCRE2_SPTR)text.data, text.length, start, 0,
                  pattern->match, context);
  pcre2_match_context_free(context);
  if (matched == PCRE2_ERROR_NOMATCH)
    return HAL_OK;
  if (matched <= 0) /* 0: the match data has no room, which cannot happen */
    return search_failed(matched == 0 ? PCRE2_ERROR_INTERNAL : matched, where,
                         failure);
  const PCRE2_SIZE *vector = pcre2_get_ovector_pointer(pattern->match);
  *begin = vector[2 * group];
  *end = vector[2 * group + 1];
  *found = true;
  return HAL_OK;
}

/* re.search of text with a pattern given as source. */
static hal_status_t contains(const char *source, jb_json_span_t text,
                             jb_json_span_t where, bool *found,
                             jh_failure_t *failure) {
  pattern_t pattern;
  hal_status_t status = compile(&pattern, source, failure);
  size_t begin = 0, end = 0;
  *found = false;
  if (status == HAL_OK)
    status = search(&pattern, text, 0, 0, &begin, &end, found, where, failure);
  release(&pattern);
  return status;
}

/* re.findall of a pattern with one group: the group of each match, in
 * order, appended to found (jb_string_t elements). */
static hal_status_t find_all(const char *source, jb_json_span_t text,
                             jb_json_span_t where, jb_vector_t *found,
                             jh_failure_t *failure) {
  pattern_t pattern;
  hal_status_t status = compile(&pattern, source, failure);
  size_t start = 0, begin = 0, end = 0;
  bool matched = status == HAL_OK;
  while (status == HAL_OK && matched && start <= text.length) {
    status = search(&pattern, text, start, 1, &begin, &end, &matched, where,
                    failure);
    if (status != HAL_OK || !matched)
      break;
    const PCRE2_SIZE *vector = pcre2_get_ovector_pointer(pattern.match);
    const jb_json_span_t value = {text.data + begin, end - begin};
    status = jh_fail_storage(failure, jh_list_push(found, value));
    start = vector[1] > vector[0] ? vector[1] : vector[1] + 1;
  }
  release(&pattern);
  return status;
}

/* A group of digits as an int; false past int64_t. */
static bool digits_value(jb_json_span_t digits, int64_t *out) {
  int64_t value = 0;
  for (size_t i = 0; i < digits.length; ++i) {
    const int64_t digit = digits.data[i] - '0';
    if (value > (INT64_MAX - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  *out = value;
  return digits.length != 0;
}

/* int() of a group of digits as str() prints it: without leading zeros. */
static void put_digits(jb_string_t *out, hal_status_t *status,
                       jb_json_span_t digits) {
  size_t at = 0;
  while (at + 1 < digits.length && digits.data[at] == '0')
    ++at;
  const jb_json_span_t rest = {digits.data + at, digits.length - at};
  jh_put_span(out, status, rest);
}

/* One value of a header or sdkconfig: int() of the digits when it fits
 * int64_t; a larger int equals no registry value. */
typedef struct {
  bool fits;
  int64_t value;
  jb_string_t digits;
} header_value_t;

static bool header_equals(const header_value_t *value,
                          jb_json_tree_view_t json) {
  return value->fits && json_equals(json, value->value);
}

/* _value: the group of the one match of source in text; a ClockError
 * "<where>: expected one match of <repr>, found [...]" otherwise. out->digits
 * must be an initialized string. */
static hal_status_t one_value(const char *source, jb_json_span_t text,
                              const char *where, header_value_t *out,
                              jh_failure_t *failure) {
  jb_vector_t found = {0};
  hal_status_t status = jh_fail_storage(failure, jh_list_init(&found));
  if (status == HAL_OK)
    status = find_all(source, text, jh_span(where), &found, failure);
  if (status == HAL_OK && jb_vector_size(&found) != 1) {
    jb_string_t message = {0};
    status = text_init(&message);
    jh_put(&message, &status, where);
    jh_put(&message, &status, ": expected one match of ");
    put_repr(&message, &status, jh_span(source));
    jh_put(&message, &status, ", found ");
    if (status == HAL_OK)
      status = jh_repr_list(&message, &found);
    status = fail_built(failure, status, &message);
    jb_string_destroy(&message);
  } else if (status == HAL_OK) {
    const jb_json_span_t digits = jh_string_span(jh_list_at(&found, 0));
    out->fits = digits_value(digits, &out->value);
    jb_string_clear(&out->digits);
    put_digits(&out->digits, &status, digits);
    status = jh_fail_storage(failure, status);
  }
  jh_list_destroy(&found);
  return status;
}

static bool contains_text(jb_json_span_t text, jb_json_span_t needle) {
  if (needle.length == 0)
    return true;
  for (size_t i = 0; i + needle.length <= text.length; ++i)
    if (memcmp(text.data + i, needle.data, needle.length) == 0)
      return true;
  return false;
}

/* The tree name of file, or a zero view. */
static jb_json_tree_view_t tree_named(const jh_clock_file_t *file,
                                      const char *name) {
  jb_json_tree_view_t tree = {0};
  if (file != NULL)
    (void)jh_json_member(trees_of(file), name, &tree);
  return tree;
}

/* tree[a][b]...: the member path of a tree, or a zero view. */
static jb_json_tree_view_t path3(jb_json_tree_view_t value, const char *a,
                                 const char *b, const char *c) {
  jb_json_tree_view_t out = {0}, step = {0};
  if (!jh_json_member(value, a, &step))
    return out;
  if (b == NULL)
    return step;
  if (!jh_json_member(step, b, &out))
    return out;
  if (c == NULL)
    return out;
  step = out;
  out = (jb_json_tree_view_t){0};
  (void)jh_json_member(step, c, &out);
  return out;
}

/* f"RCC_PLLCFGR_<prefix>{value}<suffix>" in text, value as str() prints it. */
static hal_status_t has_setting(jb_json_span_t text, const char *prefix,
                                jb_json_tree_view_t value, const char *suffix,
                                bool *found) {
  *found = false;
  if (value.tree == NULL) /* a parameter Python would miss with KeyError */
    return HAL_OK;
  jb_string_t needle = {0};
  hal_status_t status = text_init(&needle);
  jh_put(&needle, &status, prefix);
  put_str(&needle, &status, value);
  jh_put(&needle, &status, suffix);
  *found = status == HAL_OK && contains_text(text, jh_string_span(&needle));
  jb_string_destroy(&needle);
  return status;
}

hal_status_t jh_clock_check_stm32g474(const jh_clock_registry_t *registry,
                                      const char *port_dir, size_t limit,
                                      jh_failure_t *failure) {
  jb_string_t clock = {0}, tree_h = {0}, path = {0};
  hal_status_t status = jh_fail_storage(failure, text_init(&clock));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&tree_h));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&path));
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure, jh_path_join(port_dir, "stm32g474_clock.h", &path));
  if (status == HAL_OK)
    status = read_text(jb_string_data(&path), limit, &clock, failure);
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure, jh_path_join(port_dir, "stm32g474_clock_tree.h", &path));
  if (status == HAL_OK)
    status = read_text(jb_string_data(&path), limit, &tree_h, failure);
  const jh_clock_file_t *file =
      jh_clock_registry_find(registry, jh_span("stm32g474"));
  const jb_json_tree_view_t trees =
      file != NULL ? trees_of(file) : (jb_json_tree_view_t){0};
  bool same = jh_json_count(trees) == jh_clock_stm32g474_tree_count;
  for (size_t i = 0; same && i < jh_clock_stm32g474_tree_count; ++i)
    same = tree_named(file, jh_clock_stm32g474_trees[i].tree).tree != NULL;
  if (status == HAL_OK && !same) {
    jb_vector_t names = {0}, built = {0};
    status = jh_fail_storage(failure, jh_list_init(&names));
    if (status == HAL_OK)
      status = jh_fail_storage(failure, jh_list_init(&built));
    for (size_t i = 0; status == HAL_OK && i < jh_json_count(trees); ++i)
      status = jh_fail_storage(
          failure, jh_list_push(&names, jh_json_key(jh_json_item(trees, i))));
    for (size_t i = 0; status == HAL_OK && i < jh_clock_stm32g474_tree_count;
         ++i)
      status = jh_fail_storage(
          failure,
          jh_list_push(&built, jh_span(jh_clock_stm32g474_trees[i].tree)));
    jh_list_sort(&names);
    jh_list_sort(&built);
    jb_string_t message = {0};
    if (status == HAL_OK)
      status = text_init(&message);
    jh_put(&message, &status, "stm32g474 trees ");
    if (status == HAL_OK)
      status = jh_repr_list(&message, &names);
    jh_put(&message, &status, ": the startup code builds ");
    if (status == HAL_OK)
      status = jh_repr_list(&message, &built);
    jh_put(&message, &status, " only");
    status = fail_built(failure, status, &message);
    jb_string_destroy(&message);
    jh_list_destroy(&built);
    jh_list_destroy(&names);
  }
  const jb_json_tree_view_t def = tree_named(file, "default");
  const jb_json_tree_view_t hse = tree_named(file, "hse-160mhz");
  const jb_json_span_t clock_text = jh_string_span(&clock);
  const jb_json_span_t tree_text = jh_string_span(&tree_h);
  header_value_t value = {false, 0, {0}};
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&value.digits));
  if (status == HAL_OK)
    status = one_value("#define JH_G474_HSI_CLOCK_HZ (\\d+)u", clock_text,
                       "stm32g474_clock.h", &value, failure);
  if (status == HAL_OK && !(value.fits && value.value == HSI16_HZ))
    status = fail_check(failure, JH_PARTS(jh_span("HSI16 frequency changed")));
  if (status == HAL_OK)
    status = one_value("#define JH_G474_HSE_CLOCK_HZ (\\d+)u", clock_text,
                       "stm32g474_clock.h", &value, failure);
  if (status == HAL_OK && !header_equals(&value, path3(hse, "requiredSources",
                                                       "hse", "frequencyHz")))
    status = fail_check(failure, JH_PARTS(jh_span("HSE frequency differs")));
  jb_vector_t cores = {0};
  if (status == HAL_OK)
    status = jh_fail_storage(failure, jh_list_init(&cores));
  if (status == HAL_OK)
    status = find_all("#define JH_G474_CORE_CLOCK_HZ (\\d+)u", clock_text,
                      jh_span("stm32g474_clock.h"), &cores, failure);
  if (status == HAL_OK) {
    int64_t first = 0, second = 0;
    const bool match =
        jb_vector_size(&cores) == 2 &&
        digits_value(jh_string_span(jh_list_at(&cores, 0)), &first) &&
        digits_value(jh_string_span(jh_list_at(&cores, 1)), &second) &&
        json_equals(path3(hse, "frequenciesHz", "sysclk", NULL), first) &&
        json_equals(path3(def, "frequenciesHz", "sysclk", NULL), second);
    if (!match) {
      jb_string_t message = {0};
      status = text_init(&message);
      jh_put(&message, &status, "core clocks [");
      for (size_t i = 0; i < jb_vector_size(&cores); ++i) {
        jh_put(&message, &status, i == 0 ? "" : ", ");
        put_digits(&message, &status, jh_string_span(jh_list_at(&cores, i)));
      }
      jh_put(&message, &status, "] differ from the registry");
      status = fail_built(failure, status, &message);
      jb_string_destroy(&message);
    }
  }
  jh_list_destroy(&cores);
  if (status == HAL_OK)
    status = one_value("#define JH_G474_FDCAN_CLOCK_HZ (\\d+)u", clock_text,
                       "stm32g474_clock.h", &value, failure);
  if (status == HAL_OK &&
      !header_equals(&value, path3(hse, "frequenciesHz", "fdcan", NULL)))
    status = fail_check(
        failure, JH_PARTS(jh_span("FDCAN clock of the HSE tree differs")));
  const jb_json_tree_view_t hp = path3(hse, "parameters", NULL, NULL);
  const jb_json_tree_view_t dp = path3(def, "parameters", NULL, NULL);
  bool n = false, q = false, r = false;
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure, has_setting(tree_text, "RCC_PLLCFGR_PLLN(",
                             path3(hp, "pllN", NULL, NULL), "u)", &n));
  if (status == HAL_OK && n)
    status = jh_fail_storage(
        failure, has_setting(tree_text, "RCC_PLLCFGR_PLLQ_DIV",
                             path3(hp, "pllQ", NULL, NULL), "", &q));
  if (status == HAL_OK && n && q)
    status = jh_fail_storage(
        failure, has_setting(tree_text, "RCC_PLLCFGR_PLLR_DIV",
                             path3(hp, "pllR", NULL, NULL), "", &r));
  if (status == HAL_OK && !(n && q && r))
    status =
        fail_check(failure, JH_PARTS(jh_span("HSE tree PLL N/Q/R differ from "
                                             "stm32g474_clock_tree.h")));
  number_t hse_hz = {false, 0, 0.0};
  number_t quotient = {false, 0, 0.0};
  number_t pll_m = {false, 0, 0.0};
  if (status == HAL_OK &&
      !(as_number(path3(hse, "requiredSources", "hse", "frequencyHz"),
                  &hse_hz) &&
        python_floor_divide(hse_hz, integer_number(4000000), &quotient) &&
        as_number(path3(hp, "pllM", NULL, NULL), &pll_m) &&
        numbers_equal(quotient, pll_m)))
    status = fail_check(
        failure, JH_PARTS(jh_span("HSE tree PLL M differs from HSE / 4 MHz")));
  bool mn = false, dr = false;
  if (status == HAL_OK && path3(dp, "pllM", NULL, NULL).tree != NULL &&
      path3(dp, "pllN", NULL, NULL).tree != NULL) {
    jb_string_t needle = {0};
    status = text_init(&needle);
    jh_put(&needle, &status, "RCC_PLLCFGR_PLLM(");
    put_str(&needle, &status, path3(dp, "pllM", NULL, NULL));
    jh_put(&needle, &status, "u) | RCC_PLLCFGR_PLLN(");
    put_str(&needle, &status, path3(dp, "pllN", NULL, NULL));
    jh_put(&needle, &status, "u)");
    mn = status == HAL_OK && contains_text(tree_text, jh_string_span(&needle));
    jb_string_destroy(&needle);
    status = jh_fail_storage(failure, status);
  }
  if (status == HAL_OK && mn)
    status = jh_fail_storage(
        failure, has_setting(tree_text, "RCC_PLLCFGR_PLLR_DIV",
                             path3(dp, "pllR", NULL, NULL), "", &dr));
  if (status == HAL_OK && !(mn && dr))
    status = fail_check(failure,
                        JH_PARTS(jh_span("default tree PLL M/N/R differ from "
                                         "stm32g474_clock_tree.h")));
  jb_string_destroy(&value.digits);
  jb_string_destroy(&path);
  jb_string_destroy(&tree_h);
  jb_string_destroy(&clock);
  return status;
}

/* The part of text after the first separator, or none. */
static bool after(jb_json_span_t text, const char *separator,
                  jb_json_span_t *out) {
  const jb_json_span_t needle = jh_span(separator);
  for (size_t i = 0; i + needle.length <= text.length; ++i)
    if (memcmp(text.data + i, needle.data, needle.length) == 0) {
      out->data = text.data + i + needle.length;
      out->length = text.length - i - needle.length;
      return true;
    }
  return false;
}

/* The part of text before the first separator, else all of it. */
static jb_json_span_t before(jb_json_span_t text, const char *separator) {
  const jb_json_span_t needle = jh_span(separator);
  for (size_t i = 0; i + needle.length <= text.length; ++i)
    if (memcmp(text.data + i, needle.data, needle.length) == 0) {
      const jb_json_span_t head = {text.data, i};
      return head;
    }
  return text;
}

static hal_status_t require_found(const char *pattern, jb_json_span_t text,
                                  const char *where, const char *message,
                                  jh_failure_t *failure) {
  bool found = false;
  hal_status_t status =
      contains(pattern, text, jh_span(where), &found, failure);
  if (status == HAL_OK && !found)
    status = fail_check(failure, JH_PARTS(jh_span(message)));
  return status;
}

/* "<target>: <text><digits><suffix>" as a ClockError. */
static hal_status_t fail_target(jh_failure_t *failure, jb_json_span_t target,
                                const char *text, const header_value_t *value,
                                const char *suffix) {
  jb_string_t message = {0};
  hal_status_t status = text_init(&message);
  jh_put_span(&message, &status, target);
  jh_put(&message, &status, ": ");
  jh_put(&message, &status, text);
  if (value != NULL)
    jh_put_span(&message, &status, jh_string_span(&value->digits));
  jh_put(&message, &status, suffix);
  status = fail_built(failure, status, &message);
  jb_string_destroy(&message);
  return status;
}

static hal_status_t check_rp_target(const jh_clock_registry_t *registry,
                                    const char *target,
                                    const header_value_t *sys_hz,
                                    const header_value_t *xosc,
                                    jh_failure_t *failure) {
  const jb_json_span_t id = jh_span(target);
  const jb_json_tree_view_t tree =
      tree_named(jh_clock_registry_find(registry, id), "default");
  const jb_json_tree_view_t p = path3(tree, "parameters", NULL, NULL);
  if (!header_equals(sys_hz, path3(tree, "frequenciesHz", "sys", NULL)))
    return fail_target(failure, id, "Pico SDK default is ", sys_hz, " Hz");
  if (!header_equals(xosc,
                     path3(tree, "requiredSources", "xosc", "frequencyHz")))
    return fail_target(failure, id, "Pico SDK XOSC is ", xosc, " Hz");
  const bool standard = sys_hz->fits && sys_hz->value == INT64_C(125000000);
  if (!(json_equals(path3(p, "pllSysVcoHz", NULL, NULL), INT64_C(1500000000)) &&
        json_equals(path3(p, "pllSysPostdiv1", NULL, NULL), standard ? 6 : 5) &&
        json_equals(path3(p, "pllSysPostdiv2", NULL, NULL), 2)))
    return fail_target(failure, id, "PLL_SYS differs", NULL, "");
  if (!(json_equals(path3(p, "pllUsbVcoHz", NULL, NULL), INT64_C(1200000000)) &&
        json_equals(path3(p, "pllUsbPostdiv1", NULL, NULL), 5) &&
        json_equals(path3(p, "pllUsbPostdiv2", NULL, NULL), 5)))
    return fail_target(failure, id, "PLL_USB differs", NULL, "");
  return HAL_OK;
}

hal_status_t jh_clock_check_pico_sdk(const jh_clock_registry_t *registry,
                                     const char *sdk_src, size_t limit,
                                     jh_failure_t *failure) {
  jb_string_t clocks = {0}, defs = {0}, path = {0};
  hal_status_t status = jh_fail_storage(failure, text_init(&clocks));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&defs));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&path));
  if (status == HAL_OK)
    status = jh_fail_storage(
        failure,
        jh_path_join(sdk_src,
                     "rp2_common/hardware_clocks/include/hardware/clocks.h",
                     &path));
  if (status == HAL_OK)
    status = read_text(jb_string_data(&path), limit, &clocks, failure);
  const jb_json_span_t text = jh_string_span(&clocks);
  jb_json_span_t block = {NULL, 0};
  if (status == HAL_OK &&
      !after(text, "PLL settings for standard 125/150 MHz system clock.",
             &block))
    status = fail_check(failure,
                        JH_PARTS(jh_span("Pico SDK standard PLL block moved")));
  block = before(block, "#endif // SYS_CLK_KHZ == 125000");
  if (status == HAL_OK &&
      !contains_text(block, jh_span("#define PLL_SYS_VCO_FREQ_HZ               "
                                    " (1500 * MHZ)")))
    status = fail_check(failure,
                        JH_PARTS(jh_span("Pico SDK standard PLL_SYS VCO is no "
                                         "longer 1500 MHz")));
  if (status == HAL_OK)
    status = require_found(
        "#if SYS_CLK_HZ == 125 \\* MHZ\\s+#define PLL_SYS_POSTDIV1\\s+6\\s+"
        "#else\\s+#define PLL_SYS_POSTDIV1\\s+5",
        block, "clocks.h",
        "Pico SDK PLL_SYS post divider 1 is no longer 6 at 125 MHz and 5 "
        "otherwise",
        failure);
  if (status == HAL_OK)
    status = require_found(
        "#define PLL_SYS_POSTDIV2\\s+2\\b", block, "clocks.h",
        "Pico SDK PLL_SYS post divider 2 is no longer 2", failure);
  static const char *const usb[][3] = {
      {"#define PLL_USB_VCO_FREQ_HZ\\s+\\(1200\\ \\*\\ MHZ\\)",
       "Pico SDK PLL_USB_VCO_FREQ_HZ is no longer (1200 * MHZ)", NULL},
      {"#define PLL_USB_POSTDIV1\\s+5",
       "Pico SDK PLL_USB_POSTDIV1 is no longer 5", NULL},
      {"#define PLL_USB_POSTDIV2\\s+5",
       "Pico SDK PLL_USB_POSTDIV2 is no longer 5", NULL}};
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(usb); ++i)
    status = require_found(usb[i][0], text, "clocks.h", usb[i][1], failure);
  static const char *const chips[] = {"rp2040", "rp2350"};
  for (size_t c = 0; status == HAL_OK && c < COUNTOF(chips); ++c) {
    const bool rp2040 = c == 0;
    char where[64];
    (void)snprintf(where, sizeof where, "%s platform_defs.h", chips[c]);
    char relative[96];
    (void)snprintf(relative, sizeof relative,
                   "%s/hardware_regs/include/hardware/platform_defs.h",
                   chips[c]);
    status = jh_fail_storage(failure, jh_path_join(sdk_src, relative, &path));
    if (status == HAL_OK)
      status = read_text(jb_string_data(&path), limit, &defs, failure);
    const jb_json_span_t defs_text = jh_string_span(&defs);
    header_value_t xosc = {false, 0, {0}}, sys_hz = {false, 0, {0}};
    if (status == HAL_OK)
      status = jh_fail_storage(failure, text_init(&xosc.digits));
    if (status == HAL_OK)
      status = jh_fail_storage(failure, text_init(&sys_hz.digits));
    if (status == HAL_OK)
      status = one_value("#define XOSC_HZ _u\\((\\d+)\\)", defs_text, where,
                         &xosc, failure);
    if (status == HAL_OK && rp2040)
      status = require_found(
          "#define PICO_USE_FASTEST_SUPPORTED_CLOCK 0\\b", defs_text, where,
          "RP2040 no longer defaults to the standard clock", failure);
    if (status == HAL_OK && rp2040) {
      jb_vector_t found = {0};
      status = jh_fail_storage(failure, jh_list_init(&found));
      if (status == HAL_OK)
        status = find_all("#define SYS_CLK_HZ _u\\((\\d+)\\)", defs_text,
                          jh_span(where), &found, failure);
      if (status == HAL_OK && jb_vector_size(&found) == 0)
        status = fail_check(
            failure,
            JH_PARTS(jh_span(where), jh_span(": no SYS_CLK_HZ definition")));
      if (status == HAL_OK) {
        const jb_json_span_t last =
            jh_string_span(jh_list_at(&found, jb_vector_size(&found) - 1));
        sys_hz.fits = digits_value(last, &sys_hz.value);
        put_digits(&sys_hz.digits, &status, last);
        status = jh_fail_storage(failure, status);
      }
      jh_list_destroy(&found);
    } else if (status == HAL_OK) {
      status = one_value("#define SYS_CLK_HZ _u\\((\\d+)\\)", defs_text, where,
                         &sys_hz, failure);
    }
    if (status == HAL_OK && rp2040)
      status = check_rp_target(registry, "rp2040", &sys_hz, &xosc, failure);
    if (status == HAL_OK && !rp2040)
      status = check_rp_target(registry, "rp2350-arm", &sys_hz, &xosc, failure);
    if (status == HAL_OK && !rp2040)
      status =
          check_rp_target(registry, "rp2350-riscv", &sys_hz, &xosc, failure);
    jb_string_destroy(&sys_hz.digits);
    jb_string_destroy(&xosc.digits);
  }
  jb_string_destroy(&path);
  jb_string_destroy(&defs);
  jb_string_destroy(&clocks);
  return status;
}

/* SDK_CLOCK_DEFINITION of clock_registry.py. */
#define SDK_CLOCK_MACRO                                                        \
  "(?:SYS_CLK_(?:HZ|KHZ|MHZ)|XOSC_(?:HZ|KHZ|MHZ)|PLL_(?:SYS|USB|COMMON)_\\w+|" \
  "USB_CLK_(?:HZ|KHZ|MHZ)|PICO_USE_FASTEST_SUPPORTED_CLOCK)"
static const char sdk_clock_definition[] =
    "#\\s*define\\s+" SDK_CLOCK_MACRO "\\b|-D\\s*" SDK_CLOCK_MACRO "\\b|"
    "compile_definitions\\s*\\([^)]*\\b" SDK_CLOCK_MACRO "\\b";

typedef struct {
  const pattern_t *pattern;
  size_t limit;
  jh_failure_t *failure;
  jb_string_t text;
  bool found;
} override_scan_t;

static bool scanned_suffix(const char *name) {
  static const char *const suffixes[] = {".cmake", ".txt", ".h", ".c", ".cpp"};
  const char *dot = strrchr(name, '.');
  if (dot == NULL || dot == name) /* Path.suffix: none for ".h" */
    return false;
  for (size_t i = 0; i < COUNTOF(suffixes); ++i)
    if (strcmp(dot, suffixes[i]) == 0)
      return true;
  return false;
}

static hal_status_t collect_entry(void *context, const char *name,
                                  const jb_fs_info_t *info) {
  (void)info;
  return jh_list_push(context, jh_span(name));
}

/* The match in path relative to the scanned root: "<relative> sets a Pico
 * SDK clock macro: <match>". relative uses the separator of the host. */
static hal_status_t scan_file(override_scan_t *scan, const char *path,
                              jb_json_span_t relative) {
  hal_status_t status =
      read_text(path, scan->limit, &scan->text, scan->failure);
  const jb_json_span_t text = jh_string_span(&scan->text);
  size_t begin = 0, end = 0;
  bool found = false;
  if (status == HAL_OK)
    status = search(scan->pattern, text, 0, 0, &begin, &end, &found, relative,
                    scan->failure);
  if (status != HAL_OK || !found)
    return status;
  scan->found = true;
  const jb_json_span_t match = {text.data + begin, end - begin};
  return fail_check(
      scan->failure,
      JH_PARTS(relative, jh_span(" sets a Pico SDK clock macro: "), match));
}

/* Path.rglob("*") of directory in sorted order: each name, then the contents
 * of a directory that is not a link. */
static hal_status_t scan_directory(override_scan_t *scan, const char *directory,
                                   jb_json_span_t relative) {
  jb_vector_t names = {0};
  jb_string_t path = {0}, child = {0};
  hal_status_t status = jh_fail_storage(scan->failure, jh_list_init(&names));
  if (status == HAL_OK)
    status = jh_fail_storage(scan->failure, text_init(&path));
  if (status == HAL_OK)
    status = jh_fail_storage(scan->failure, text_init(&child));
  if (status == HAL_OK) {
    const hal_status_t listed = jb_fs_list(directory, collect_entry, &names);
    if (listed == HAL_ENOMEM)
      status = jh_fail_storage(scan->failure, listed);
    else if (listed != HAL_OK && listed != HAL_ENOENT && listed != HAL_ETYPE)
      status = jh_fail(scan->failure, listed, 3,
                       JH_PARTS(jh_span(directory), jh_span(": cannot list: "),
                                jh_span(hal_status_to_string(listed))));
    jh_list_sort(&names);
  }
#ifdef _WIN32
  static const char separator[] = "\\";
#else
  static const char separator[] = "/";
#endif
  for (size_t i = 0;
       status == HAL_OK && !scan->found && i < jb_vector_size(&names); ++i) {
    const char *name = jb_string_data(jh_list_at(&names, i));
    status =
        jh_fail_storage(scan->failure, jh_path_join(directory, name, &path));
    if (status == HAL_OK)
      status = jh_fail_storage(
          scan->failure, jh_join(&child, JH_PARTS(relative, jh_span(separator),
                                                  jh_span(name))));
    jb_fs_info_t entry, target;
    if (status != HAL_OK ||
        jb_fs_status(jb_string_data(&path), false, &entry) != HAL_OK)
      continue;
    const bool link = entry.kind == JB_FS_LINK;
    const bool followed =
        jb_fs_status(jb_string_data(&path), true, &target) == HAL_OK;
    if (followed && target.kind == JB_FS_REGULAR && scanned_suffix(name))
      status = scan_file(scan, jb_string_data(&path), jh_string_span(&child));
    else if (!link && entry.kind == JB_FS_DIRECTORY)
      status =
          scan_directory(scan, jb_string_data(&path), jh_string_span(&child));
  }
  jb_string_destroy(&child);
  jb_string_destroy(&path);
  jh_list_destroy(&names);
  return status;
}

hal_status_t jh_clock_check_no_hal_override(const char *root, size_t limit,
                                            jh_failure_t *failure) {
  static const char *const bases[] = {"cmake", "link_libraries", "src",
                                      "config", "examples"};
  pattern_t pattern;
  hal_status_t status = compile(&pattern, sdk_clock_definition, failure);
  override_scan_t scan = {&pattern, limit, failure, {0}, false};
  jb_string_t path = {0};
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&scan.text));
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&path));
  for (size_t i = 0; status == HAL_OK && !scan.found && i < COUNTOF(bases);
       ++i) {
    status = jh_fail_storage(failure, jh_path_join(root, bases[i], &path));
    if (status == HAL_OK)
      status = scan_directory(&scan, jb_string_data(&path), jh_span(bases[i]));
  }
  jb_string_destroy(&path);
  jb_string_destroy(&scan.text);
  if (pattern.code != NULL)
    release(&pattern);
  return status;
}

hal_status_t jh_clock_check_esp_sdkconfig(const jh_clock_registry_t *registry,
                                          jb_json_span_t target,
                                          const char *path, size_t limit,
                                          jh_failure_t *failure) {
  const jh_clock_file_t *file = jh_clock_registry_find(registry, target);
  if (file == NULL)
    return fail_check(failure,
                      JH_PARTS(jh_span("--esp-sdkconfig names "), target,
                               jh_span(", which has no clock registry")));
  const jb_json_tree_view_t params =
      path3(tree_named(file, "default"), "parameters", NULL, NULL);
  jb_string_t text = {0};
  hal_status_t status = jh_fail_storage(failure, text_init(&text));
  if (status == HAL_OK)
    status = read_text(path, limit, &text, failure);
  static const char *const options[][3] = {
      {"(?m)^CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=(\\d+)$",
       "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ", "cpuFreqMhz"},
      {"(?m)^CONFIG_XTAL_FREQ=(\\d+)$", "CONFIG_XTAL_FREQ", "xtalFreqMhz"}};
  header_value_t value = {false, 0, {0}};
  if (status == HAL_OK)
    status = jh_fail_storage(failure, text_init(&value.digits));
  for (size_t i = 0; status == HAL_OK && i < COUNTOF(options); ++i) {
    status =
        one_value(options[i][0], jh_string_span(&text), path, &value, failure);
    jb_json_tree_view_t wanted = {0};
    if (status == HAL_OK && !jh_json_member(params, options[i][2], &wanted))
      status = fail_check(
          failure, JH_PARTS(target, jh_span(": the default tree has no "),
                            jh_span(options[i][2])));
    if (status == HAL_OK && !header_equals(&value, wanted)) {
      jb_string_t message = {0};
      status = text_init(&message);
      jh_put_span(&message, &status, target);
      jh_put(&message, &status, ": ");
      jh_put(&message, &status, options[i][1]);
      jh_put(&message, &status, "=");
      jh_put_span(&message, &status, jh_string_span(&value.digits));
      jh_put(&message, &status, ", registry ");
      jh_put(&message, &status, options[i][2]);
      jh_put(&message, &status, "=");
      put_str(&message, &status, wanted);
      status = fail_built(failure, status, &message);
      jb_string_destroy(&message);
    }
  }
  jb_string_destroy(&value.digits);
  jb_string_destroy(&text);
  return status;
}
