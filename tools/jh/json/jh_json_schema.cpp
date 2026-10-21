#include "jh_json_schema.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <jb/array.h>
#include <jb/json_number.h>
#include <jb/json_pointer.h>
#include <jb/utf8.h>
#include <limits>
#include <memory_resource>
#include <new>
#include <string>
#include <string_view>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {
using node = jb_json_node_t;
using view = std::string_view;
using text = std::pmr::string;
constexpr node absent{nullptr, 0};
struct stopped {
  hal_status_t status;
};
void check(hal_status_t s) {
  if (s != HAL_OK)
    throw stopped{s};
}
void expect(bool yes, hal_status_t s = HAL_EINVAL) {
  if (!yes)
    throw stopped{s};
}
size_t add(size_t a, size_t b) {
  expect(b <= SIZE_MAX - a, HAL_EOVERFLOW);
  return a + b;
}
size_t mul(size_t a, size_t b) {
  expect(a == 0 || b <= SIZE_MAX / a, HAL_EOVERFLOW);
  return a * b;
}
jb_json_span_t span(view s) { return {s.data(), s.size()}; }
view bytes(jb_json_span_t s) {
  return {s.data == nullptr ? "" : s.data, s.length};
}
hal_status_t callback_status(const hal_status_t &s) {
  // C can return any int representation. Inspect its bytes before a C++ enum
  // load, whose permitted range is narrower than the ABI's 32-bit storage.
  int32_t code;
  std::memcpy(&code, &s, sizeof(code));
  if (code == HAL_OK)
    return HAL_OK;
  if (code >= HAL_ETYPE && code <= HAL_EINVAL)
    return static_cast<hal_status_t>(code);
  return HAL_EINTERNAL;
}

// Each evaluation frame reclaims its temporaries after copying retained
// reports. No allocator falls back to the process heap, including PCRE2's
// allocator.
struct arena : std::pmr::memory_resource {
  unsigned char *data;
  size_t capacity, used = 0;
  arena(void *p, size_t n)
      : data(static_cast<unsigned char *>(p)), capacity(n) {}
  void *take(size_t n, size_t alignment = alignof(std::max_align_t)) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(data) + used;
    const size_t padding = (alignment - address % alignment) % alignment;
    const size_t end = add(add(used, padding), n);
    expect(end <= capacity, HAL_EOVERFLOW);
    void *out = data + used + padding;
    used = end;
    return out;
  }
  void *do_allocate(size_t n, size_t a) override { return take(n, a); }
  void do_deallocate(void *, size_t, size_t) override {}
  bool
  do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
    return this == &other;
  }
};
struct frame {
  arena &storage;
  size_t saved;
  explicit frame(arena &a) : storage(a), saved(a.used) {}
  ~frame() { storage.used = saved; }
};
struct value {
  node n;
  bool key = false;
};
struct active {
  node schema;
  value instance;
};
struct source {
  jh_json_schema_source_t input;
};
struct mark {
  size_t count, bytes;
};
struct engine {
  const jh_json_schema_limits_t &limit;
  size_t remaining, depth = 0, source_count = 0, schema_nodes = 0;
  size_t report_count = 0;
  source *sources;
  active *stack;
  jh_json_schema_error_t *reports;
  arena temp, report_text, regex_memory;
  jh_json_schema_load_fn load;
  void *load_user;

  engine(const jh_json_schema_limits_t &l, arena &all,
         jh_json_schema_load_fn loader, void *user)
      : limit(l), remaining(l.max_work),
        sources(static_cast<source *>(
            all.take(mul(l.max_schema_nodes, sizeof(source))))),
        stack(static_cast<active *>(
            all.take(mul(l.max_ref_depth, sizeof(active))))),
        reports(static_cast<jh_json_schema_error_t *>(
            all.take(mul(l.max_reports, sizeof(jh_json_schema_error_t))))),
        temp(all.take(temp_size(l)), temp_size(l)),
        report_text(all.take(l.max_report_bytes), l.max_report_bytes),
        regex_memory(all.take(l.max_regex_bytes), l.max_regex_bytes),
        load(loader), load_user(user) {}

  static size_t temp_size(const jh_json_schema_limits_t &l) {
    return mul(add(l.max_ref_depth, 2),
               add(add(mul(l.max_value_bytes, 32), mul(l.max_pointer_bytes, 4)),
                   add(l.max_uri_bytes, 8192)));
  }
  void charge(size_t n = 1) {
    expect(n <= remaining, HAL_EOVERFLOW);
    remaining -= n;
  }
  jb_json_type_t kind(value v) const {
    return v.key ? static_cast<jb_json_type_t>(JB_JSON_STRING)
                 : v.n.doc->tokens[v.n.index].type;
  }
  jb_json_type_t kind(node n) const { return kind(value{n}); }
  bool boolean(node n) {
    bool b;
    check(jb_json_get_bool(n, &b));
    return b;
  }
  size_t count(node n) const { return n.doc->tokens[n.index].child_count; }
  node first(node n) const {
    return {n.doc, n.doc->tokens[n.index].first_child};
  }
  node next(node n) const {
    return {n.doc, n.doc->tokens[n.index].next_sibling};
  }
  bool exists(node n) const {
    return n.doc != nullptr && n.index != JB_JSON_NODE_NONE;
  }
  text string(value v) {
    const auto &token = v.n.doc->tokens[v.n.index];
    const size_t encoded = v.key ? token.key_length : token.length;
    expect(encoded <= add(mul(limit.max_value_bytes, 6), 2), HAL_EOVERFLOW);
    charge(add(mul(encoded, 2), 1));
    size_t n;
    const auto copy = v.key ? jb_json_key_copy : jb_json_string_copy;
    check(copy(v.n, nullptr, 0, &n));
    expect(n <= limit.max_value_bytes, HAL_EOVERFLOW);
    charge(add(n, 1));
    text out(&temp);
    out.resize(n);
    check(copy(v.n, out.data(), n, &n));
    return out;
  }
  text string(node n) { return string(value{n}); }
  text key(node n) { return string(value{n, true}); }
  node find(node object, view name) {
    if (!exists(object))
      return absent;
    expect(kind(object) == JB_JSON_OBJECT);
    for (node child = first(object); exists(child); child = next(child)) {
      frame local(temp);
      charge();
      if (key(child) == name)
        return child;
    }
    return absent;
  }
  text raw(node n) {
    jb_json_span_t s;
    check(jb_json_raw(n, &s));
    expect(s.length <= limit.max_value_bytes, HAL_EOVERFLOW);
    charge(add(s.length, 1));
    return text(bytes(s), &temp);
  }
  text joined(view at, view token) {
    text out(at, &temp);
    out += '/';
    for (char c : token) {
      charge();
      if (c == '~')
        out += "~0";
      else if (c == '/')
        out += "~1";
      else
        out += c;
      expect(out.size() <= limit.max_pointer_bytes, HAL_EOVERFLOW);
    }
    expect(out.size() <= limit.max_pointer_bytes, HAL_EOVERFLOW);
    return out;
  }
  text decimal(size_t n) {
    char buf[3 * sizeof(size_t) + 1];
    size_t i = COUNTOF(buf);
    do {
      buf[--i] = static_cast<char>('0' + n % 10);
      n /= 10;
    } while (n != 0);
    return text(buf + i, COUNTOF(buf) - i, &temp);
  }
  size_t natural(node n) {
    expect(n.doc->tokens[n.index].length <= limit.max_value_bytes,
           HAL_EOVERFLOW);
    charge(add(n.doc->tokens[n.index].length, 1));
    uint64_t out;
    check(jb_json_get_u64(n, &out));
    expect(out <= SIZE_MAX, HAL_EOVERFLOW);
    return static_cast<size_t>(out);
  }
  int compare(node a, node b) {
    frame local(temp);
    expect(a.doc->tokens[a.index].length <= limit.max_value_bytes &&
               b.doc->tokens[b.index].length <= limit.max_value_bytes,
           HAL_EOVERFLOW);
    const size_t length =
        add(a.doc->tokens[a.index].length, b.doc->tokens[b.index].length);
    expect(length <= mul(limit.max_value_bytes, 2), HAL_EOVERFLOW);
    // Bounds decimal scans and arbitrary-size exponent arithmetic in jb::base.
    const size_t allowance =
        add(256, add(mul(length, 64), mul(mul(length, length), 4)));
    charge(allowance);
    size_t needed;
    check(jb_json_number_compare_workspace_size(a, b, &needed));
    void *scratch = temp.take(needed);
    int result;
    check(jb_json_number_compare(a, b, {scratch, needed}, allowance, &result));
    return result;
  }
  bool equal(value a, value b, size_t nested = 0) {
    frame local(temp);
    charge();
    expect(nested < limit.max_ref_depth, HAL_EOVERFLOW);
    if (kind(a) != kind(b))
      return false;
    switch (kind(a)) {
    case JB_JSON_NULL:
      return true;
    case JB_JSON_BOOL:
      return boolean(a.n) == boolean(b.n);
    case JB_JSON_NUMBER:
      return compare(a.n, b.n) == 0;
    case JB_JSON_STRING:
      return string(a) == string(b);
    default:
      break;
    }
    if (count(a.n) != count(b.n))
      return false;
    node right = first(b.n);
    for (node left = first(a.n); exists(left); left = next(left)) {
      frame member(temp);
      charge();
      if (kind(a) == JB_JSON_OBJECT)
        right = find(b.n, key(left));
      if (!exists(right) || !equal({left}, {right}, nested + 1))
        return false;
      if (kind(a) == JB_JSON_ARRAY)
        right = next(right);
    }
    return true;
  }
  text quoted(view s) {
    text out(&temp);
    const char quote =
        s.find('\'') != view::npos && s.find('"') == view::npos ? '"' : '\'';
    out += quote;
    constexpr char hex[] = "0123456789abcdef";
    for (char byte : s) {
      const auto c = static_cast<unsigned char>(byte);
      charge();
      if (c == '\\' || c == static_cast<unsigned char>(quote)) {
        out += '\\';
        out += static_cast<char>(c);
      } else if (c == '\n')
        out += "\\n";
      else if (c == '\r')
        out += "\\r";
      else if (c == '\t')
        out += "\\t";
      else if (c < 32 || c == 127) {
        out += "\\x";
        out += hex[c >> 4];
        out += hex[c & 15];
      } else
        out += static_cast<char>(c);
    }
    out += quote;
    return out;
  }
  text repr(node n, size_t nested = 0) {
    text result(&temp);
    repr_into(n, result, nested);
    return result;
  }
  void repr_into(node n, text &out, size_t nested) {
    charge();
    expect(nested < limit.max_ref_depth, HAL_EOVERFLOW);
    switch (kind(n)) {
    case JB_JSON_NULL:
      out += "None";
      break;
    case JB_JSON_BOOL:
      out += boolean(n) ? "True" : "False";
      break;
    case JB_JSON_STRING:
      out += quoted(string(n));
      break;
    case JB_JSON_NUMBER:
      out += raw(n);
      break;
    default: {
      const bool object = kind(n) == JB_JSON_OBJECT;
      out += object ? '{' : '[';
      bool separator = false;
      for (node child = first(n); exists(child); child = next(child)) {
        charge();
        if (separator)
          out += ", ";
        separator = true;
        if (object) {
          out += quoted(key(child));
          out += ": ";
        }
        repr_into(child, out, nested + 1);
        expect(out.size() <= limit.max_report_bytes, HAL_EOVERFLOW);
      }
      out += object ? '}' : ']';
    }
    }
  }

  static void *regex_alloc(PCRE2_SIZE n, void *user) noexcept {
    try {
      auto &owner = *static_cast<engine *>(user);
      // A short quantified group can emit a large compiled program. Charge
      // allocation requests as well as the input-length compilation estimate.
      owner.charge(n);
      return owner.regex_memory.take(n);
    } catch (...) {
      return nullptr;
    }
  }
  static void regex_free(void *, void *) noexcept {}
  struct regex_budget {
    engine &owner;
    size_t step;
  };
  static int callout(pcre2_callout_block *, void *user) noexcept {
    auto &budget = *static_cast<regex_budget *>(user);
    auto &e = budget.owner;
    // A quantified literal may scan the remaining subject between callouts.
    // Charge that scan at every step, including each unanchored restart.
    if (e.remaining < budget.step)
      return PCRE2_ERROR_CALLOUT;
    e.remaining -= budget.step;
    return 0;
  }
  static int compile_guard(uint32_t depth_value, void *user) noexcept {
    return depth_value > static_cast<engine *>(user)->limit.max_ref_depth ? 1
                                                                          : 0;
  }
  text pattern(view original) {
    expect(original.size() <= limit.max_pattern_bytes, HAL_EOVERFLOW);
    text out(&temp);
    bool in_class = false;
    // Python str.isspace()/re \s: include the four extra C0 separators.
    constexpr view space =
        "\\u0009-\\u000d\\u001c-\\u0020\\u0085\\u00a0\\u1680\\u2000-"
        "\\u200a\\u2028-\\u2029\\u202f\\u205f\\u3000";
    for (size_t i = 0; i < original.size(); ++i) {
      charge();
      char c = original[i];
      if (c == '\\' && i + 1 < original.size()) {
        c = original[++i];
        if (c == 's') {
          if (!in_class)
            out += '[';
          out += space;
          if (!in_class)
            out += ']';
        } else if (c == 'v') {
          out += "\\u000b";
        } else {
          // Only Python constructs whose semantics are implemented here.
          const bool alphabetic =
              (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
          expect(!alphabetic || c == 'x' || c == 'u' || c == 'n' || c == 'r' ||
                     c == 't' || c == 'f' || c == 'v' || c == 'a',
                 HAL_EUNSUPPORTED);
          expect(c < '0' || c > '9', HAL_EUNSUPPORTED);
          if (c == 'x' || c == 'u') {
            const size_t digits = c == 'x' ? 2 : 4;
            expect(original.size() - i > digits, HAL_EUNSUPPORTED);
            for (size_t j = 1; j <= digits; ++j) {
              const char h = original[i + j];
              expect((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') ||
                         (h >= 'A' && h <= 'F'),
                     HAL_EUNSUPPORTED);
            }
          }
          out += '\\';
          out += c;
        }
      } else {
        if (c == '[') {
          if (in_class) {
            expect(i + 1 == original.size() || original[i + 1] != ':',
                   HAL_EUNSUPPORTED);
          } else {
            size_t first = i + 1;
            if (first < original.size() && original[first] == '^')
              ++first;
            expect(first == original.size() || original[first] != ']',
                   HAL_EUNSUPPORTED);
            in_class = true;
          }
        }
        if (c == ']')
          in_class = false;
        if (c == '(' && i + 1 < original.size()) {
          expect(original[i + 1] != '*', HAL_EUNSUPPORTED);
          if (original[i + 1] == '?') {
            expect(i + 2 < original.size() &&
                       (original[i + 2] == ':' || original[i + 2] == '!' ||
                        original[i + 2] == '='),
                   HAL_EUNSUPPORTED);
          }
        }
        // Keep backtracking-control extensions outside the supported subset.
        if (c == '+' && i != 0)
          expect(original[i - 1] != '+' && original[i - 1] != '*' &&
                     original[i - 1] != '?' && original[i - 1] != '}',
                 HAL_EUNSUPPORTED);
        out += c;
      }
    }
    return out;
  }
  bool search(view original, view subject) {
    frame local(temp), regex_frame(regex_memory);
    expect(subject.size() <= limit.max_value_bytes, HAL_EOVERFLOW);
    auto adapted = pattern(original);
    charge(add(64, mul(add(adapted.size(), 1), add(adapted.size(), 1))));
    auto *general = pcre2_general_context_create(regex_alloc, regex_free, this);
    expect(general != nullptr, HAL_EOVERFLOW);
    auto *compile = pcre2_compile_context_create(general);
    expect(compile != nullptr, HAL_EOVERFLOW);
    check(pcre2_set_newline(compile, PCRE2_NEWLINE_LF) == 0 ? HAL_OK
                                                            : HAL_EINTERNAL);
    (void)pcre2_set_compile_recursion_guard(compile, compile_guard, this);
    (void)pcre2_set_max_pattern_length(compile, adapted.size());
    (void)pcre2_set_max_pattern_compiled_length(compile, limit.max_regex_bytes);
    int error;
    PCRE2_SIZE offset;
    auto *code = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(adapted.data()), adapted.size(),
        PCRE2_UTF | PCRE2_UCP | PCRE2_ALT_BSUX | PCRE2_AUTO_CALLOUT |
            PCRE2_NO_AUTO_POSSESS | PCRE2_NO_START_OPTIMIZE,
        &error, &offset, compile);
    if (code == nullptr) {
      expect(error != PCRE2_ERROR_HEAP_FAILED &&
                 error != PCRE2_ERROR_PARENTHESES_NEST_TOO_DEEP &&
                 error != PCRE2_ERROR_PARENTHESES_STACK_CHECK &&
                 error != PCRE2_ERROR_PATTERN_TOO_LARGE &&
                 error != PCRE2_ERROR_PATTERN_COMPILED_SIZE_TOO_BIG &&
                 error != PCRE2_ERROR_PATTERN_STRING_TOO_LONG,
             HAL_EOVERFLOW);
      throw stopped{HAL_EUNSUPPORTED};
    }
    auto *match = pcre2_match_context_create(general);
    auto *data = pcre2_match_data_create_from_pattern(code, general);
    expect(match != nullptr && data != nullptr, HAL_EOVERFLOW);
    (void)pcre2_set_match_limit(match, limit.regex_match_limit);
    (void)pcre2_set_depth_limit(match, limit.regex_depth_limit);
    (void)pcre2_set_heap_limit(match, limit.regex_heap_limit_kib);
    regex_budget budget{*this, add(add(subject.size(), adapted.size()), 1)};
    (void)pcre2_set_callout(match, callout, &budget);
    charge(add(subject.size(), 1));
    const int result =
        pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(subject.data()),
                    subject.size(), 0, 0, data, match);
    pcre2_match_data_free(data);
    pcre2_match_context_free(match);
    pcre2_code_free(code);
    pcre2_compile_context_free(compile);
    pcre2_general_context_free(general);
    if (result >= 0)
      return true;
    if (result == PCRE2_ERROR_NOMATCH)
      return false;
    if (result == PCRE2_ERROR_MATCHLIMIT || result == PCRE2_ERROR_DEPTHLIMIT ||
        result == PCRE2_ERROR_HEAPLIMIT || result == PCRE2_ERROR_NOMEMORY ||
        result == PCRE2_ERROR_CALLOUT)
      throw stopped{HAL_EOVERFLOW};
    throw stopped{HAL_EUNSUPPORTED};
  }

  bool type(value v, view name) {
    const auto k = kind(v);
    if (name == "null")
      return k == JB_JSON_NULL;
    if (name == "boolean")
      return k == JB_JSON_BOOL;
    if (name == "string")
      return k == JB_JSON_STRING;
    if (name == "object")
      return k == JB_JSON_OBJECT;
    if (name == "array")
      return k == JB_JSON_ARRAY;
    if (name == "number")
      return k == JB_JSON_NUMBER;
    expect(name == "integer", HAL_EUNSUPPORTED);
    if (k != JB_JSON_NUMBER)
      return false;
    charge(add(v.n.doc->tokens[v.n.index].length, 1));
    bool integral;
    check(jb_json_number_is_integer(v.n, &integral));
    return integral;
  }
  void check_schema(node s, size_t nested = 0) {
    frame local(temp);
    charge();
    expect(nested < limit.max_ref_depth, HAL_EOVERFLOW);
    if (kind(s) == JB_JSON_BOOL)
      return;
    expect(kind(s) == JB_JSON_OBJECT);
    constexpr view supported[] = {"$schema",
                                  "$defs",
                                  "$ref",
                                  "title",
                                  "description",
                                  "type",
                                  "properties",
                                  "required",
                                  "additionalProperties",
                                  "propertyNames",
                                  "enum",
                                  "const",
                                  "pattern",
                                  "minimum",
                                  "maximum",
                                  "minLength",
                                  "items",
                                  "minItems",
                                  "uniqueItems",
                                  "minProperties",
                                  "maxProperties",
                                  "allOf",
                                  "oneOf",
                                  "not",
                                  "if",
                                  "then"};
    for (node r = first(s); exists(r); r = next(r)) {
      frame member(temp);
      const auto name = key(r);
      charge(COUNTOF(supported));
      expect(std::find(std::begin(supported), std::end(supported),
                       view(name)) != std::end(supported),
             HAL_EUNSUPPORTED);
      if (name == "properties" || name == "$defs") {
        expect(kind(r) == JB_JSON_OBJECT);
        for (node c = first(r); exists(c); c = next(c))
          check_schema(c, nested + 1);
      } else if (name == "allOf" || name == "oneOf") {
        expect(kind(r) == JB_JSON_ARRAY);
        for (node c = first(r); exists(c); c = next(c))
          check_schema(c, nested + 1);
      } else if (name == "items" || name == "additionalProperties" ||
                 name == "propertyNames" || name == "not" || name == "if" ||
                 name == "then") {
        check_schema(r, nested + 1);
      } else if (name == "type") {
        const auto validate_type = [&](node t) {
          expect(kind(t) == JB_JSON_STRING);
          const auto n = string(t);
          constexpr view names[] = {"null",   "boolean", "number", "integer",
                                    "string", "array",   "object"};
          expect(std::find(std::begin(names), std::end(names), view(n)) !=
                     std::end(names),
                 HAL_EUNSUPPORTED);
        };
        if (kind(r) == JB_JSON_ARRAY) {
          expect(count(r) != 0);
          for (node c = first(r); exists(c); c = next(c))
            validate_type(c);
        } else
          validate_type(r);
      } else if (name == "required") {
        expect(kind(r) == JB_JSON_ARRAY);
        for (node c = first(r); exists(c); c = next(c)) {
          expect(kind(c) == JB_JSON_STRING);
          (void)string(c);
        }
      } else if (name == "enum")
        expect(kind(r) == JB_JSON_ARRAY);
      else if (name == "uniqueItems")
        expect(kind(r) == JB_JSON_BOOL);
      else if (name == "minimum" || name == "maximum")
        expect(kind(r) == JB_JSON_NUMBER);
      else if (name == "minLength" || name == "minItems" ||
               name == "minProperties" || name == "maxProperties")
        (void)natural(r);
      else if (name == "pattern") {
        expect(kind(r) == JB_JSON_STRING);
        (void)search(string(r), "");
      } else if (name == "$ref" || name == "$schema" || name == "title" ||
                 name == "description") {
        expect(kind(r) == JB_JSON_STRING);
        (void)string(r);
      }
    }
  }
  size_t register_source(const jh_json_schema_source_t &s) {
    expect(s.doc != nullptr && s.doc->valid && s.doc->token_count != 0);
    expect(s.uri.data != nullptr && s.uri.length != 0);
    expect(s.uri.length <= limit.max_uri_bytes, HAL_EOVERFLOW);
    charge(add(s.uri.length, 1));
    check(jb_utf8_validate(s.uri.data, s.uri.length, nullptr));
    expect(bytes(s.uri).find('#') == view::npos &&
           bytes(s.uri).find('\0') == view::npos);
    for (size_t i = 0; i < source_count; ++i) {
      charge(add(s.uri.length, 1));
      if (bytes(sources[i].input.uri) == bytes(s.uri)) {
        expect(sources[i].input.doc == s.doc);
        return i;
      }
      expect(sources[i].input.doc != s.doc);
    }
    expect(s.doc->token_count <= limit.max_schema_nodes - schema_nodes,
           HAL_EOVERFLOW);
    schema_nodes += s.doc->token_count;
    const size_t index = source_count++;
    new (&sources[index]) source{s};
    check_schema({s.doc, 0});
    return index;
  }
  jb_json_span_t retain(view s) {
    charge(add(s.size(), 1));
    char *out = static_cast<char *>(report_text.take(s.size(), 1));
    if (!s.empty())
      std::memcpy(out, s.data(), s.size());
    return {out, s.size()};
  }
  void report(node rule, view at, view reason, view message, bool name_error) {
    frame local(temp);
    expect(report_count < limit.max_reports, HAL_EOVERFLOW);
    size_t size;
    // Pointer traversal scans parents/siblings; charge the whole document
    // bound.
    charge(add(rule.doc->token_count, rule.doc->input.length));
    check(jb_json_pointer_copy(rule, nullptr, 0, &size));
    expect(size <= limit.max_pointer_bytes, HAL_EOVERFLOW);
    text pointer(&temp);
    pointer.resize(size);
    check(jb_json_pointer_copy(rule, pointer.data(), pointer.size(), &size));
    jb_json_span_t uri{};
    for (size_t i = 0; i < source_count; ++i)
      if (sources[i].input.doc == rule.doc) {
        uri = sources[i].input.uri;
        break;
      }
    expect(uri.data != nullptr, HAL_EINTERNAL);
    text full(&temp);
    if (name_error)
      full += "field name: ";
    full += message;
    const jh_json_schema_error_t result{retain(at),      retain(bytes(uri)),
                                        retain(pointer), rule,
                                        retain(reason),  retain(full)};
    new (&reports[report_count++]) jh_json_schema_error_t(result);
  }
  mark checkpoint() const { return {report_count, report_text.used}; }
  void restore(mark m) {
    report_count = m.count;
    report_text.used = m.bytes;
  }
  node resolve(size_t &document, node reference) {
    const auto ref = string(reference);
    const view whole(ref);
    const size_t hash = whole.find('#');
    const view target = whole.substr(0, hash);
    if (!target.empty()) {
      expect(load != nullptr, HAL_ENOENT);
      jh_json_schema_source_t loaded{};
      check(callback_status(
          load(load_user, sources[document].input.uri, span(target), &loaded)));
      document = register_source(loaded);
    }
    const auto *doc = sources[document].input.doc;
    node out{doc, 0};
    if (hash != view::npos) {
      const view encoded = whole.substr(hash + 1);
      text decoded(&temp);
      const auto hex = [](char c) -> unsigned {
        if (c >= '0' && c <= '9')
          return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f')
          return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
          return static_cast<unsigned>(c - 'A' + 10);
        throw stopped{HAL_EINVAL};
      };
      for (size_t i = 0; i < encoded.size(); ++i) {
        charge();
        if (encoded[i] == '%') {
          expect(encoded.size() - i > 2);
          decoded +=
              static_cast<char>(hex(encoded[i + 1]) * 16 + hex(encoded[i + 2]));
          i += 2;
        } else
          decoded += encoded[i];
      }
      expect(decoded.size() <= limit.max_pointer_bytes, HAL_EOVERFLOW);
      const view fragment(decoded);
      check(jb_utf8_validate(fragment.data(), fragment.size(), nullptr));
      charge(add(doc->input.length, doc->token_count));
      check(jb_json_pointer_get(doc, span(fragment), &out));
    }
    check_schema(out);
    return out;
  }
  void evaluate(size_t document, node s, value v, view at, node keep = absent,
                bool name_error = false) {
    frame local(temp);
    charge();
    expect(depth < limit.max_ref_depth, HAL_EOVERFLOW);
    for (size_t i = 0; i < depth; ++i) {
      charge();
      const auto &a = stack[i];
      expect(!(a.schema.doc == s.doc && a.schema.index == s.index &&
               a.instance.n.doc == v.n.doc && a.instance.n.index == v.n.index &&
               a.instance.key == v.key),
             HAL_EINVAL);
    }
    new (&stack[depth++]) active{s, v};
    struct depth_guard {
      size_t &d;
      ~depth_guard() { --d; }
    } guard{depth};
    const auto fail = [&](node rule, view reason, view message, view pointer) {
      report(rule, pointer, reason, message, name_error);
    };
    if (kind(s) == JB_JSON_BOOL) {
      if (!boolean(s))
        fail(s, "false", "no value is allowed here", at);
      return;
    }
    node rule = find(s, "$ref");
    if (exists(rule)) {
      size_t target_doc = document;
      const node target = resolve(target_doc, rule);
      evaluate(target_doc, target, v, at, absent, name_error);
    }
    rule = find(s, "type");
    if (exists(rule)) {
      bool matches = false;
      text names(&temp);
      const auto visit = [&](node t) {
        auto name = string(t);
        if (!names.empty())
          names += " or ";
        names += name;
        if (type(v, name))
          matches = true;
      };
      if (kind(rule) == JB_JSON_ARRAY) {
        for (node t = first(rule); exists(t); t = next(t))
          visit(t);
      } else
        visit(rule);
      if (!matches) {
        fail(rule, "type", text("type: expected ", &temp) + names, at);
        return;
      }
    }
    rule = find(s, "const");
    if (exists(rule) && !equal(v, {rule}))
      fail(rule, "const", text("expected ", &temp) + repr(rule), at);
    rule = find(s, "enum");
    if (exists(rule)) {
      bool matches = false;
      for (node c = first(rule); exists(c); c = next(c))
        if (equal(v, {c})) {
          matches = true;
          break;
        }
      if (!matches)
        fail(rule, "enum", text("expected one of ", &temp) + repr(rule), at);
    }
    if (kind(v) == JB_JSON_STRING) {
      const auto input = string(v);
      rule = find(s, "minLength");
      if (exists(rule)) {
        size_t length = 0;
        for (char byte : input) {
          const auto c = static_cast<unsigned char>(byte);
          charge();
          if ((c & 0xc0U) != 0x80U)
            ++length;
        }
        if (length < natural(rule))
          fail(rule, "minLength", text("shorter than ", &temp) + raw(rule), at);
      }
      rule = find(s, "pattern");
      if (exists(rule)) {
        const auto regex = string(rule);
        if (!search(regex, input))
          fail(rule, "pattern", text("does not match ", &temp) + quoted(regex),
               at);
      }
    }
    if (kind(v) == JB_JSON_NUMBER) {
      rule = find(s, "minimum");
      if (exists(rule) && compare(v.n, rule) < 0)
        fail(rule, "minimum", text("below ", &temp) + raw(rule), at);
      rule = find(s, "maximum");
      if (exists(rule) && compare(v.n, rule) > 0)
        fail(rule, "maximum", text("above ", &temp) + raw(rule), at);
    }
    if (kind(v) == JB_JSON_ARRAY) {
      rule = find(s, "minItems");
      if (exists(rule) && count(v.n) < natural(rule))
        fail(rule, "minItems",
             text("fewer than ", &temp) + raw(rule) + " items", at);
      const node unique = find(s, "uniqueItems"), items = find(s, "items");
      size_t index = 0;
      for (node c = first(v.n); exists(c); c = next(c), ++index) {
        frame member(temp);
        charge();
        const auto child_at = joined(at, decimal(index));
        if (exists(unique) && boolean(unique)) {
          for (node earlier = first(v.n); earlier.index != c.index;
               earlier = next(earlier)) {
            if (equal({c}, {earlier})) {
              fail(unique, "uniqueItems", "duplicate item", child_at);
              break;
            }
          }
        }
      }
      index = 0;
      if (exists(items))
        for (node c = first(v.n); exists(c); c = next(c), ++index) {
          frame member(temp);
          evaluate(document, items, {c}, joined(at, decimal(index)), absent,
                   name_error);
        }
    }
    if (kind(v) == JB_JSON_OBJECT)
      object_errors(document, s, v.n, at, keep, name_error);
    rule = find(s, "allOf");
    if (exists(rule))
      for (node c = first(rule); exists(c); c = next(c))
        evaluate(document, c, v, at, absent, name_error);
    rule = find(s, "oneOf");
    if (exists(rule)) {
      const mark base = checkpoint();
      size_t passing = 0, typed = 0;
      for (node c = first(rule); exists(c); c = next(c)) {
        const mark start = checkpoint();
        evaluate(document, c, v, at, absent, name_error);
        if (report_count == start.count)
          ++passing;
        else {
          bool root_type = false;
          for (size_t i = start.count; i < report_count; ++i) {
            charge();
            if (bytes(reports[i].instance_pointer) == at &&
                bytes(reports[i].reason_code) == "type")
              root_type = true;
          }
          if (!root_type)
            ++typed;
          if (!root_type && typed == 1)
            continue;
        }
        restore(start);
      }
      if (passing == 1)
        restore(base);
      else if (passing > 1) {
        restore(base);
        fail(rule, "oneOf", "matches more than one form", at);
      } else if (typed != 1) {
        restore(base);
        fail(rule, "oneOf", "matches none of the allowed forms", at);
      }
    }
    rule = find(s, "not");
    if (exists(rule)) {
      const mark start = checkpoint();
      evaluate(document, rule, v, at, absent, name_error);
      const bool matched = report_count == start.count;
      restore(start);
      if (matched)
        fail(rule, "not", "matches a forbidden form", at);
    }
    rule = find(s, "if");
    if (exists(rule)) {
      const mark start = checkpoint();
      evaluate(document, rule, v, at, absent, name_error);
      const bool matched = report_count == start.count;
      restore(start);
      const node then = find(s, "then");
      if (matched && exists(then))
        evaluate(document, then, v, at, rule, name_error);
    }
  }
  bool kept(node condition, view name) {
    if (!exists(condition) || kind(condition) != JB_JSON_OBJECT)
      return false;
    if (exists(find(find(condition, "properties"), name)))
      return true;
    const node required = find(condition, "required");
    if (exists(required))
      for (node c = first(required); exists(c); c = next(c)) {
        frame local(temp);
        if (string(c) == name)
          return true;
      }
    return false;
  }
  void object_errors(size_t document, node s, node object, view at, node keep,
                     bool name_error) {
    const node required = find(s, "required");
    if (exists(required))
      for (node c = first(required); exists(c); c = next(c)) {
        frame local(temp);
        auto name = string(c);
        if (!exists(find(object, name)))
          report(required, joined(at, name), "required",
                 "required field is missing", name_error);
      }
    const node properties = find(s, "properties"),
               additional = find(s, "additionalProperties"),
               names = find(s, "propertyNames");
    for (node c = first(object); exists(c); c = next(c)) {
      frame local(temp);
      const auto name = key(c), path = joined(at, name);
      const node property = find(properties, name);
      if (exists(property))
        evaluate(document, property, {c}, path, absent, name_error);
      else if (exists(additional)) {
        if (kind(additional) == JB_JSON_BOOL && !boolean(additional))
          report(additional, path, "additionalProperties", "unknown field",
                 name_error);
        else
          evaluate(document, additional, {c}, path, absent, name_error);
      }
      if (exists(names))
        evaluate(document, names, {c, true}, path, absent, true);
    }
    node rule = find(s, "minProperties");
    if (exists(rule) && count(object) < natural(rule))
      report(rule, at, "minProperties",
             text("fewer than ", &temp) + raw(rule) + " fields", name_error);
    rule = find(s, "maxProperties");
    if (exists(rule) && count(object) > natural(rule)) {
      node extra = absent;
      for (node c = first(object); exists(c); c = next(c)) {
        frame local(temp);
        const auto name = key(c);
        if (!kept(keep, name) && (!exists(extra) || name < key(extra)))
          extra = c;
      }
      const auto target =
          exists(extra) ? joined(at, key(extra)) : text(at, &temp);
      report(rule, target, "maxProperties",
             text("more than ", &temp) + raw(rule) + " fields", name_error);
    }
  }
  int pointer_order(view a, view b) {
    while (!a.empty() && !b.empty()) {
      charge();
      a.remove_prefix(1);
      b.remove_prefix(1);
      const auto ae = a.find('/'), be = b.find('/');
      view x = a.substr(0, ae), y = b.substr(0, be);
      charge(add(x.size(), y.size()));
      const auto numeric = [](view t) {
        return !t.empty() && std::all_of(t.begin(), t.end(), [](char c) {
          return c >= '0' && c <= '9';
        });
      };
      const bool xn = numeric(x), yn = numeric(y);
      if (xn != yn)
        return xn ? -1 : 1;
      if (xn) {
        while (x.size() > 1 && x.front() == '0')
          x.remove_prefix(1);
        while (y.size() > 1 && y.front() == '0')
          y.remove_prefix(1);
        if (x.size() != y.size())
          return x.size() < y.size() ? -1 : 1;
      }
      if (x != y)
        return x < y ? -1 : 1;
      a = ae == view::npos ? view{} : a.substr(ae);
      b = be == view::npos ? view{} : b.substr(be);
    }
    return a.empty() ? (b.empty() ? 0 : -1) : 1;
  }
  void sort_reports() {
    // Stable insertion sort uses no hidden allocation and charges every
    // compare.
    for (size_t i = 1; i < report_count; ++i) {
      auto current = reports[i];
      size_t j = i;
      while (j != 0 &&
             pointer_order(bytes(current.instance_pointer),
                           bytes(reports[j - 1].instance_pointer)) < 0) {
        charge();
        reports[j] = reports[j - 1];
        --j;
      }
      reports[j] = current;
    }
  }
};

size_t workspace_size(const jh_json_schema_limits_t &l) {
  expect(l.max_ref_depth <= 128);
  size_t n = mul(l.max_schema_nodes, sizeof(source));
  n = add(n, mul(l.max_ref_depth, sizeof(active)));
  n = add(n, mul(l.max_reports, sizeof(jh_json_schema_error_t)));
  n = add(n, engine::temp_size(l));
  n = add(n, l.max_report_bytes);
  return add(add(n, l.max_regex_bytes), 6 * alignof(std::max_align_t));
}
} // namespace

extern "C" hal_status_t
jh_json_schema_workspace_size(const jh_json_schema_limits_t *limits,
                              size_t *out_bytes) {
  if (limits == nullptr || out_bytes == nullptr)
    return HAL_EINVAL;
  try {
    const size_t n = workspace_size(*limits);
    *out_bytes = n;
    return HAL_OK;
  } catch (const stopped &e) {
    return e.status;
  } catch (...) {
    return HAL_EINTERNAL;
  }
}
extern "C" hal_status_t jh_json_schema_validate(
    const jb_json_doc_t *instance, const jh_json_schema_source_t *schema,
    jh_json_schema_load_fn load, void *load_user,
    jh_json_schema_report_fn report, void *report_user,
    jb_json_workspace_t work, const jh_json_schema_limits_t *limits) {
  if (instance == nullptr || schema == nullptr || limits == nullptr ||
      (work.data == nullptr && work.size != 0))
    return HAL_EINVAL;
  try {
    const size_t needed = workspace_size(*limits);
    expect(work.data != nullptr && work.size >= needed, HAL_EOVERFLOW);
    expect(instance->valid && instance->token_count != 0);
    expect(instance->token_count <= limits->max_instance_nodes, HAL_EOVERFLOW);
    arena all(work.data, work.size);
    engine e(*limits, all, load, load_user);
    const size_t root = e.register_source(*schema);
    e.evaluate(root, {schema->doc, 0}, {{instance, 0}}, "");
    e.sort_reports();
    if (report != nullptr)
      for (size_t i = 0; i < e.report_count; ++i)
        check(callback_status(report(report_user, &e.reports[i])));
    return e.report_count == 0 ? HAL_OK : HAL_ECONFIG;
  } catch (const stopped &e) {
    return e.status;
  } catch (const std::bad_alloc &) {
    return HAL_ENOMEM;
  } catch (...) {
    return HAL_EINTERNAL;
  }
}
