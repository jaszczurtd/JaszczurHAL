#include "jh_project_config.h"
#include "../json/jh_json_file.h"
#include "../json/jh_python_repr.h"
#include <algorithm>
#include <filesystem>
#include <jb/file.h>
#include <jb/unicode.h>
#include <jb/utf8.h>
#include <limits>
#include <system_error>

namespace jh::config {
namespace {

constexpr const char *header_name = "hal_project_config.h";
constexpr const char *variants_macro = "JH_PROJECT_VARIANTS";
constexpr const char *targets_macro = "JH_PROJECT_TARGETS";
const std::vector<std::string> target_selectors = {
    "HAL_TARGET_RP2040",    "HAL_TARGET_RP2350_ARM", "HAL_TARGET_RP2350_RISCV",
    "HAL_TARGET_STM32G474", "HAL_TARGET_ESP32",      "HAL_TARGET_ESP32_S3",
    "HAL_TARGET_MOCK"};
// As in project_config.py.
constexpr size_t max_include_depth = 16;
// Limits of jh where Python has none or stops on its recursion limit.
constexpr size_t max_file_bytes = 16 * 1024 * 1024;
constexpr size_t max_macro_nesting = 1000;
constexpr size_t max_condition_nesting = 256;
constexpr int64_t int64_max = std::numeric_limits<int64_t>::max();
constexpr int64_t int64_min = std::numeric_limits<int64_t>::min();

/* A computed value outside int64_t; the caller decides what it means. */
struct range_error {
  std::string detail;
};

[[noreturn]] void reject(const std::string &where, const char *code,
                         const std::string &detail,
                         hal_status_t status = HAL_ECONFIG, int exit_code = 2) {
  throw failure{status, exit_code, where + ": [" + code + "] " + detail,
                code,   where,     {}};
}

[[noreturn]] void limit(const std::string &where, const std::string &detail) {
  reject(where, "JH-CFG-SCOPE", detail, HAL_EOVERFLOW, 4);
}

/* Tokens a read, or one query on its result, may still visit. */
struct work_budget {
  size_t &remaining;
  size_t total;
  void charge(const std::string &where, size_t amount) {
    if (amount > remaining)
      limit(where, "expansion and dependency tracking exceed " +
                       std::to_string(total) + " tokens");
    remaining -= amount;
  }
};

// ── Text ────────────────────────────────────────────────────────────────────
// Files are valid UTF-8; whitespace is str.isspace() of Python, everything
// else the reader classifies is ASCII.

uint32_t scalar_at(const std::string &text, size_t offset, size_t *length) {
  uint32_t scalar = 0;
  size_t used = 1;
  if (jb_utf8_decode(text.data() + offset, text.size() - offset, &scalar,
                     &used) != HAL_OK) {
    scalar = static_cast<unsigned char>(text[offset]);
    used = 1;
  }
  *length = used;
  return scalar;
}

size_t scalar_length(const std::string &text, size_t offset) {
  size_t used;
  (void)scalar_at(text, offset, &used);
  return used;
}

bool space_at(const std::string &text, size_t offset, size_t *length) {
  const auto byte = static_cast<unsigned char>(text[offset]);
  if (byte < 0x80) {
    // str.isspace() of ASCII: \t to \r, the separators 0x1C to 0x1F, space.
    *length = 1;
    return (byte >= 0x09 && byte <= 0x0D) || (byte >= 0x1C && byte <= 0x20);
  }
  const uint32_t scalar = scalar_at(text, offset, length);
  jb_unicode_properties_t properties;
  return jb_unicode_properties(scalar, &properties) == HAL_OK &&
         properties.whitespace;
}

size_t skip_space(const std::string &text, size_t offset) {
  size_t used;
  while (offset < text.size() && space_at(text, offset, &used))
    offset += used;
  return offset;
}

size_t previous_scalar(const std::string &text, size_t end, size_t floor) {
  size_t start = end - 1;
  while (start > floor &&
         (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80)
    --start;
  return start;
}

std::string rstrip(const std::string &text) {
  size_t end = text.size(), used;
  while (end > 0) {
    const size_t start = previous_scalar(text, end, 0);
    if (!space_at(text, start, &used))
      break;
    end = start;
  }
  return text.substr(0, end);
}

std::string strip(const std::string &text) {
  const size_t begin = skip_space(text, 0);
  return rstrip(text.substr(begin));
}

bool ascii_letter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
bool ascii_digit(char c) { return c >= '0' && c <= '9'; }
bool name_start(char c) { return ascii_letter(c) || c == '_'; }
bool name_part(char c) { return name_start(c) || ascii_digit(c); }
bool hex_digit(char c) {
  return ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool identifier(const std::string &text) {
  if (text.empty() || !name_start(text[0]))
    return false;
  return std::all_of(text.begin() + 1, text.end(), name_part);
}

bool feature_name(const std::string &name) {
  for (const char *prefix : {"HAL_ENABLE_", "HAL_DISABLE_"}) {
    const std::string start(prefix);
    if (name.size() > start.size() && name.compare(0, start.size(), start) == 0)
      return std::all_of(
          name.begin() + static_cast<std::ptrdiff_t>(start.size()), name.end(),
          [](char c) {
            return (c >= 'A' && c <= 'Z') || ascii_digit(c) || c == '_';
          });
  }
  return false;
}

bool contains(const std::vector<std::string> &items, const std::string &item) {
  return std::find(items.begin(), items.end(), item) != items.end();
}

bool is_target_selector(const std::string &name) {
  return contains(target_selectors, name);
}

// ── Files ───────────────────────────────────────────────────────────────────

/* Path.resolve() of Python: absolute, symbolic links of existing parts
 * resolved, the rest normalized. */
std::string resolved(const std::string &path) {
  std::error_code error;
  auto result =
      std::filesystem::weakly_canonical(std::filesystem::u8path(path), error);
  if (error)
    result = std::filesystem::absolute(std::filesystem::u8path(path), error)
                 .lexically_normal();
  return result.u8string();
}

bool regular_file(const std::string &path) {
  std::error_code error;
  return std::filesystem::is_regular_file(std::filesystem::u8path(path), error);
}

std::string parent_of(const std::string &path) {
  return std::filesystem::u8path(path).parent_path().u8string();
}

std::string joined(const std::string &directory, const std::string &name) {
  return (std::filesystem::u8path(directory) / std::filesystem::u8path(name))
      .u8string();
}

std::string read_text(const std::string &path) {
  file_owner owner;
  const hal_status_t status =
      jb_file_read(path.c_str(), max_file_bytes, &owner.data);
  if (status != HAL_OK)
    reject(path, "JH-CFG-SCOPE",
           std::string("cannot read: ") + hal_status_to_string(status), status,
           status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : 3);
  std::string text(reinterpret_cast<const char *>(owner.data.data),
                   owner.data.length);
  size_t bad = 0;
  if (jb_utf8_validate(text.data(), text.size(), &bad) != HAL_OK)
    reject(path, "JH-CFG-SCOPE",
           "is not valid UTF-8 at byte " + std::to_string(bad), HAL_EPROTO);
  return text;
}

// ── Logical lines ───────────────────────────────────────────────────────────

struct logical_line {
  size_t number;
  std::string text;
};

/* Comment-free preprocessing lines and their source line, exactly as
 * preprocessor_logical_lines() of the Python reader splits them. */
std::vector<logical_line> logical_lines(const std::string &input) {
  std::string text;
  text.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\r') {
      text += '\n';
      if (i + 1 < input.size() && input[i + 1] == '\n')
        ++i;
    } else {
      text += input[i];
    }
  }
  std::string spliced;
  std::vector<size_t> lines;
  size_t line = 1;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '\n') {
      ++line;
      i += 2;
      continue;
    }
    spliced += text[i];
    lines.push_back(line);
    if (text[i] == '\n')
      ++line;
    ++i;
  }

  std::vector<logical_line> result;
  std::string buffer;
  size_t origin = 0;
  char quote = 0;
  size_t offset = 0;
  const auto append = [&](size_t at, size_t length) {
    size_t used;
    if (origin == 0 && !space_at(spliced, at, &used))
      origin = lines[at];
    buffer.append(spliced, at, length);
  };
  const auto emit = [&](size_t at) {
    result.push_back({origin != 0 ? origin : lines[at], buffer});
    buffer.clear();
    origin = 0;
  };
  while (offset < spliced.size()) {
    const char c = spliced[offset];
    size_t used = scalar_length(spliced, offset);
    if (quote != 0) {
      append(offset, used);
      if (c == '\\' && offset + 1 < spliced.size()) {
        offset += used;
        used = scalar_length(spliced, offset);
        append(offset, used);
      } else if (c == quote) {
        quote = 0;
      } else if (c == '\n') {
        emit(offset);
        quote = 0;
      }
      offset += used;
      continue;
    }
    const char next = offset + 1 < spliced.size() ? spliced[offset + 1] : 0;
    if (c == '/' && next == '/') {
      buffer += ' ';
      const size_t newline = spliced.find('\n', offset + 2);
      offset = newline == std::string::npos ? spliced.size() : newline;
      continue;
    }
    if (c == '/' && next == '*') {
      buffer += ' ';
      const size_t end = spliced.find("*/", offset + 2);
      offset = end == std::string::npos ? spliced.size() : end + 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      append(offset, 1);
      ++offset;
      continue;
    }
    if (c == '\n') {
      emit(offset);
      ++offset;
      continue;
    }
    append(offset, used);
    offset += used;
  }
  if (!buffer.empty())
    result.push_back(
        {origin != 0 ? origin : (lines.empty() ? 1 : lines.back()), buffer});
  return result;
}

struct source_line {
  std::string path;
  size_t number;
  std::string where() const { return path + ":" + std::to_string(number); }
};

/* ^\s*#\s*([A-Za-z_]+)\b(.*)$ of the Python reader. A name followed by an
 * ASCII digit is no directive there; one followed by another non-ASCII
 * scalar than whitespace depends on Unicode word classes, which jh rejects. */
bool split_directive(const std::string &text, const source_line &line,
                     std::string *name, std::string *argument) {
  size_t i = skip_space(text, 0);
  if (i >= text.size() || text[i] != '#')
    return false;
  i = skip_space(text, i + 1);
  const size_t start = i;
  while (i < text.size() && name_start(text[i]))
    ++i;
  if (i == start)
    return false;
  if (i < text.size()) {
    size_t used;
    if (ascii_digit(text[i]))
      return false;
    if (static_cast<unsigned char>(text[i]) >= 0x80 &&
        !space_at(text, i, &used))
      reject(line.where(), "JH-CFG-SCOPE",
             "non-ASCII character after #" + text.substr(start, i - start));
  }
  *name = text.substr(start, i - start);
  *argument = strip(text.substr(i));
  return true;
}

// ── Expressions ─────────────────────────────────────────────────────────────

size_t literal_suffix(const std::string &text, size_t i) {
  while (i < text.size() &&
         std::string("uUlL").find(text[i]) != std::string::npos)
    ++i;
  return i;
}

/* End of the condition token at i, or i: the alternatives of _TOKEN in
 * their order (number, character, name, operator). */
size_t token_end(const std::string &text, size_t i) {
  const size_t size = text.size();
  const char c = text[i];
  const char next = i + 1 < size ? text[i + 1] : 0;
  if (c == '0' && (next == 'x' || next == 'X') && i + 2 < size &&
      hex_digit(text[i + 2])) {
    size_t j = i + 2;
    while (j < size && hex_digit(text[j]))
      ++j;
    return literal_suffix(text, j);
  }
  if (c == '0' && (next == 'b' || next == 'B') && i + 2 < size &&
      (text[i + 2] == '0' || text[i + 2] == '1')) {
    size_t j = i + 2;
    while (j < size && (text[j] == '0' || text[j] == '1'))
      ++j;
    return literal_suffix(text, j);
  }
  if (ascii_digit(c)) {
    size_t j = i;
    while (j < size && ascii_digit(text[j]))
      ++j;
    return literal_suffix(text, j);
  }
  if (c == '\'') {
    size_t j = i + 1, items = 0;
    while (j < size && text[j] != '\'') {
      if (text[j] == '\\') {
        if (j + 1 >= size)
          break;
        j += 1 + scalar_length(text, j + 1);
      } else {
        j += scalar_length(text, j);
      }
      ++items;
    }
    if (items != 0 && j < size && text[j] == '\'')
      return j + 1;
  }
  if (name_start(c)) {
    size_t j = i + 1;
    while (j < size && name_part(text[j]))
      ++j;
    return j;
  }
  for (const char *op : {"||", "&&", "==", "!=", "<=", ">=", "<<", ">>"})
    if (c == op[0] && next == op[1])
      return i + 2;
  if (std::string("-+*/%<>!~&|^?:(),").find(c) != std::string::npos)
    return i + 1;
  return i;
}

std::vector<std::string> condition_tokens(const std::string &input,
                                          const std::string &source) {
  const std::string text = strip(input);
  std::vector<std::string> tokens;
  size_t offset = 0;
  while (offset < text.size()) {
    const size_t start = skip_space(text, offset);
    const size_t end = start < text.size() ? token_end(text, start) : start;
    if (end == start)
      reject(source, "JH-CFG-SCOPE",
             "cannot evaluate " + python_repr(text.substr(offset)));
    tokens.push_back(text.substr(start, end - start));
    offset = end;
  }
  return tokens;
}

struct literal {
  int64_t value;
  bool in_range;
};

uint32_t character_value(const std::string &token) {
  size_t used;
  if (token[1] != '\\')
    return scalar_at(token, 1, &used);
  switch (token[2]) {
  case 'n':
    return 10;
  case 't':
    return 9;
  case 'r':
    return 13;
  case '0':
    return 0;
  case '\\':
    return 92;
  case '\'':
    return 39;
  case '"':
    return 34;
  default:
    return scalar_at(token, 2, &used);
  }
}

literal integer_literal(const std::string &token, const std::string &source) {
  if (token[0] == '\'')
    return {static_cast<int64_t>(character_value(token)), true};
  std::string digits = token;
  while (!digits.empty() &&
         std::string("uUlL").find(digits.back()) != std::string::npos)
    digits.pop_back();
  unsigned base = 10;
  size_t start = 0;
  if (digits.size() > 1 && digits[0] == '0' &&
      (digits[1] == 'x' || digits[1] == 'X')) {
    base = 16;
    start = 2;
  } else if (digits.size() > 1 && digits[0] == '0' &&
             (digits[1] == 'b' || digits[1] == 'B')) {
    base = 2;
    start = 2;
  } else if (digits.size() > 1 && digits[0] == '0') {
    base = 8;
  }
  uint64_t value = 0;
  bool in_range = true;
  for (size_t i = start; i < digits.size(); ++i) {
    const char c = digits[i];
    const unsigned digit = ascii_digit(c)           ? unsigned(c - '0')
                           : (c >= 'a' && c <= 'f') ? unsigned(c - 'a' + 10)
                                                    : unsigned(c - 'A' + 10);
    // Python int(text, 8) refuses 8 and 9; it raises ValueError, not the
    // reader's own error, so no caller turns it into "no value".
    if (digit >= base)
      reject(source, "JH-CFG-SCOPE",
             "invalid octal literal " + python_repr(token), HAL_EPROTO);
    if (value > (static_cast<uint64_t>(int64_max) - digit) / base)
      in_range = false;
    else
      value = value * base + digit;
  }
  return {in_range ? static_cast<int64_t>(value) : 0, in_range};
}

std::string decimal(int64_t value) { return std::to_string(value); }

[[noreturn]] void out_of_range(const std::string &detail) {
  throw range_error{detail};
}

int64_t from_bits(uint64_t bits) {
  // Two's complement without implementation-defined conversion.
  if (bits <= static_cast<uint64_t>(int64_max))
    return static_cast<int64_t>(bits);
  return -static_cast<int64_t>(~bits) - 1;
}

class parser {
  std::vector<std::string> tokens_;
  size_t index_ = 0;
  const std::string &source_;
  size_t nesting_ = 0;

  [[noreturn]] void fail(const std::string &detail) const {
    reject(source_, "JH-CFG-SCOPE", detail);
  }
  const std::string *peek() const {
    return index_ < tokens_.size() ? &tokens_[index_] : nullptr;
  }
  const std::string &take(const char *expected = nullptr) {
    const std::string *token = peek();
    if (token == nullptr || (expected != nullptr && *token != expected))
      fail(std::string("expected ") + (expected ? expected : "operand") +
           " in condition");
    ++index_;
    return *token;
  }
  /* One more nested construct: parentheses, a unary operator, a ?: branch. */
  struct nested {
    parser &owner;
    explicit nested(parser &p) : owner(p) {
      if (++owner.nesting_ > max_condition_nesting)
        limit(owner.source_, "condition nests more than " +
                                 std::to_string(max_condition_nesting) +
                                 " levels");
    }
    ~nested() { --owner.nesting_; }
    nested(const nested &) = delete;
    nested &operator=(const nested &) = delete;
  };

  int64_t conditional(bool live) {
    const int64_t condition = binary(0, live);
    if (peek() == nullptr || *peek() != "?")
      return condition;
    take("?");
    nested guard(*this);
    const int64_t when_true = conditional(live && condition != 0);
    take(":");
    const int64_t when_false = conditional(live && condition == 0);
    return condition != 0 ? when_true : when_false;
  }

  static bool at_level(size_t level, const std::string &op) {
    static const std::vector<std::vector<std::string>> levels = {
        {"||"},
        {"&&"},
        {"|"},
        {"^"},
        {"&"},
        {"==", "!="},
        {"<", ">", "<=", ">="},
        {"<<", ">>"},
        {"+", "-"},
        {"*", "/", "%"}};
    return contains(levels[level], op);
  }
  static constexpr size_t level_count = 10;

  int64_t binary(size_t level, bool live) {
    if (level == level_count)
      return unary(live);
    int64_t value = binary(level + 1, live);
    while (peek() != nullptr && at_level(level, *peek())) {
      const std::string op = take();
      const bool decided =
          (op == "&&" && value == 0) || (op == "||" && value != 0);
      const int64_t right = binary(level + 1, live && !decided);
      value = apply(op, value, right, live);
    }
    return value;
  }

  static int64_t shift(const std::string &op, int64_t left, int64_t right) {
    std::string direction = op;
    uint64_t count;
    if (right >= 0) {
      count = static_cast<uint64_t>(right);
    } else {
      // Negative counts follow GCC's preprocessor extension.
      direction = op == "<<" ? ">>" : "<<";
      count = static_cast<uint64_t>(-(right + 1)) + 1;
    }
    if (direction == ">>") {
      if (count >= 64)
        return left < 0 ? -1 : 0;
      return left >= 0 ? left >> count : ~((~left) >> count);
    }
    if (left == 0)
      return 0;
    if (count > 63)
      out_of_range(decimal(left) + " " + op + " " + decimal(right));
    const int64_t largest = int64_max >> count;
    if (left > largest || left < -largest - 1)
      out_of_range(decimal(left) + " " + op + " " + decimal(right));
    return from_bits(static_cast<uint64_t>(left) << count);
  }

  static bool multiply_overflows(int64_t left, int64_t right) {
    if (left == 0 || right == 0)
      return false;
    if (left > 0)
      return right > 0 ? left > int64_max / right : right < int64_min / left;
    return right > 0 ? left < int64_min / right : right < int64_max / left;
  }

  int64_t apply(const std::string &op, int64_t left, int64_t right, bool live) {
    if (!live)
      return 0; // C parses an operand it skips but computes nothing.
    const auto range = [&]() {
      out_of_range(decimal(left) + " " + op + " " + decimal(right));
    };
    if ((op == "/" || op == "%") && right == 0)
      fail("division by zero in condition");
    if (op == "<<" || op == ">>")
      return shift(op, left, right);
    if (op == "/") {
      if (left == int64_min && right == -1)
        range();
      return left / right;
    }
    if (op == "%")
      return right == -1 ? 0 : left % right;
    if (op == "+") {
      if ((right > 0 && left > int64_max - right) ||
          (right < 0 && left < int64_min - right))
        range();
      return left + right;
    }
    if (op == "-") {
      if ((right < 0 && left > int64_max + right) ||
          (right > 0 && left < int64_min + right))
        range();
      return left - right;
    }
    if (op == "*") {
      if (multiply_overflows(left, right))
        range();
      return left * right;
    }
    const auto bits_left = static_cast<uint64_t>(left);
    const auto bits_right = static_cast<uint64_t>(right);
    if (op == "|")
      return from_bits(bits_left | bits_right);
    if (op == "^")
      return from_bits(bits_left ^ bits_right);
    if (op == "&")
      return from_bits(bits_left & bits_right);
    if (op == "||")
      return left != 0 || right != 0 ? 1 : 0;
    if (op == "&&")
      return left != 0 && right != 0 ? 1 : 0;
    if (op == "==")
      return left == right ? 1 : 0;
    if (op == "!=")
      return left != right ? 1 : 0;
    if (op == "<")
      return left < right ? 1 : 0;
    if (op == ">")
      return left > right ? 1 : 0;
    if (op == "<=")
      return left <= right ? 1 : 0;
    return left >= right ? 1 : 0;
  }

  int64_t unary(bool live) {
    const std::string &token = take();
    if (token == "!" || token == "~" || token == "-" || token == "+" ||
        token == "(") {
      nested guard(*this);
      if (token == "(") {
        const int64_t value = conditional(live);
        take(")");
        return value;
      }
      const int64_t value = unary(live);
      if (token == "!")
        return value == 0 ? 1 : 0;
      if (token == "~")
        return ~value;
      if (token == "-") {
        if (!live)
          return 0;
        if (value == int64_min)
          out_of_range("-(" + decimal(value) + ")");
        return -value;
      }
      return value;
    }
    if (ascii_digit(token[0]) || token[0] == '\'') {
      const literal value = integer_literal(token, source_);
      if (value.in_range)
        return value.value;
      if (live)
        out_of_range("integer " + token);
      return 0;
    }
    if (identifier(token))
      return 0; // An identifier left after expansion is 0, as in C.
    fail("unexpected " + python_repr(token) + " in condition");
  }

public:
  parser(std::vector<std::string> tokens, const std::string &source)
      : tokens_(std::move(tokens)), source_(source) {}
  int64_t parse() {
    const int64_t value = conditional(true);
    if (peek() != nullptr)
      fail("unexpected " + python_repr(*peek()) + " in condition");
    return value;
  }
};

size_t defined_operand(const std::vector<std::string> &tokens, size_t index,
                       const std::string &source, std::string *name) {
  const bool parenthesized = index < tokens.size() && tokens[index] == "(";
  if (parenthesized)
    ++index;
  if (index >= tokens.size() || !identifier(tokens[index]))
    reject(source, "JH-CFG-SCOPE", "malformed defined()");
  *name = tokens[index++];
  if (parenthesized) {
    if (index >= tokens.size() || tokens[index] != ")")
      reject(source, "JH-CFG-SCOPE", "malformed defined()");
    ++index;
  }
  return index;
}

struct expansion {
  const macro_table &macros;
  const std::string &source;
  // Macros being expanded; a name maps to one entry of the table.
  std::vector<const macro *> hidden;
  std::vector<std::string> out;
  work_budget &budget;
  // Tokens of each macro body, made at its first expansion; references to
  // the stored vectors stay valid while later bodies are added.
  std::unordered_map<const macro *, std::vector<std::string>> bodies;

  /* Replace defined operators and object-like macros, C-style. */
  void run(const std::vector<std::string> &tokens) {
    if (hidden.size() > max_macro_nesting)
      limit(source, "macros nest more than " +
                        std::to_string(max_macro_nesting) + " levels");
    for (size_t index = 0; index < tokens.size();) {
      budget.charge(source, 1);
      const std::string &token = tokens[index];
      if (token == "defined") {
        std::string name;
        index = defined_operand(tokens, index + 1, source, &name);
        out.push_back(macros.find(name) != nullptr ? "1" : "0");
        continue;
      }
      const macro *definition = macros.find(token);
      if (definition != nullptr &&
          std::find(hidden.begin(), hidden.end(), definition) == hidden.end()) {
        if (definition->function_like)
          reject(source, "JH-CFG-SCOPE",
                 "function-like macro " + token +
                     " is not supported in conditions");
        auto found = bodies.find(definition);
        if (found == bodies.end())
          found = bodies
                      .emplace(definition,
                               definition->body.empty()
                                   ? std::vector<std::string>{}
                                   : condition_tokens(definition->body,
                                                      definition->source))
                      .first;
        const std::vector<std::string> &body = found->second;
        hidden.push_back(definition);
        run(body);
        hidden.pop_back();
      } else {
        out.push_back(token);
      }
      ++index;
    }
  }
};

/* Evaluate an #if expression or a macro body; range_error escapes. */
int64_t evaluate(const std::string &text, const macro_table &macros,
                 const std::string &source, work_budget &budget) {
  expansion expanded{macros, source, {}, {}, budget, {}};
  expanded.run(condition_tokens(text, source));
  return parser(std::move(expanded.out), source).parse();
}

/* Names and punctuators of a directive argument or a macro body, as _PP_TOKEN
 * splits them: numbers (1u, 0x10) and literals are not names. */
std::vector<std::string> pp_names(const std::string &text) {
  std::vector<std::string> names;
  const size_t size = text.size();
  for (size_t i = 0; i < size;) {
    const char c = text[i];
    const char next = i + 1 < size ? text[i + 1] : 0;
    if (ascii_digit(c) || (c == '.' && ascii_digit(next))) {
      size_t j = c == '.' ? i + 2 : i + 1;
      while (j < size) {
        const char d = text[j];
        if ((d == 'e' || d == 'E' || d == 'p' || d == 'P') && j + 1 < size &&
            (text[j + 1] == '+' || text[j + 1] == '-'))
          j += 2;
        else if (name_part(d) || d == '.')
          ++j;
        else
          break;
      }
      i = j;
      continue;
    }
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < size && text[j] != c)
        j += text[j] == '\\' && j + 1 < size ? 1 + scalar_length(text, j + 1)
             : text[j] == '\\'               ? size - j
                                             : scalar_length(text, j);
      if (j < size && text[j] == c) {
        i = j + 1;
        continue;
      }
    }
    if (name_start(c)) {
      size_t j = i + 1;
      while (j < size && name_part(text[j]))
        ++j;
      names.push_back(text.substr(i, j - i));
      i = j;
      continue;
    }
    size_t used;
    if (space_at(text, i, &used)) {
      i += used;
      continue;
    }
    names.push_back(text.substr(i, used));
    i += used;
  }
  return names;
}

/* Add every name start uses to found: the operand of defined, and each
 * object-like macro with the names its body expands to. entered holds the
 * macros not to expand (the one whose value is asked). Each macro body is
 * scanned once: the Python reader enters a macro again on every path to it,
 * which only repeats names it already found, at a cost that can grow
 * exponentially with the depth of the macros. */
void referenced_names(const std::vector<std::string> &start,
                      const macro_table &macros, std::set<std::string> entered,
                      std::set<std::string> &found, const std::string &where,
                      work_budget &budget) {
  std::vector<std::vector<std::string>> pending{start};
  while (!pending.empty()) {
    const std::vector<std::string> tokens = std::move(pending.back());
    pending.pop_back();
    budget.charge(where, tokens.size());
    for (size_t index = 0; index < tokens.size();) {
      const std::string &token = tokens[index];
      if (token == "defined") {
        ++index;
        if (index < tokens.size() && tokens[index] == "(")
          ++index;
        if (index < tokens.size() && identifier(tokens[index]))
          found.insert(tokens[index]);
        ++index;
        continue;
      }
      if (identifier(token)) {
        found.insert(token);
        const macro *definition = macros.find(token);
        if (definition != nullptr && !definition->function_like &&
            entered.insert(token).second)
          pending.push_back(pp_names(definition->body));
      }
      ++index;
    }
  }
}

// ── Declarations ────────────────────────────────────────────────────────────

/* Split a macro argument list on top-level commas. */
std::vector<std::string> split_arguments(const std::string &text) {
  std::vector<std::string> arguments;
  int depth = 0;
  char quote = 0;
  std::string current;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (quote != 0) {
      current += c;
      if (c == '\\' && i + 1 < text.size()) {
        current += text[++i];
      } else if (c == quote) {
        quote = 0;
      }
    } else if (c == '"' || c == '\'') {
      quote = c;
      current += c;
    } else if (c == '(') {
      ++depth;
      current += c;
    } else if (c == ')') {
      --depth;
      current += c;
    } else if (c == ',' && depth == 0) {
      arguments.push_back(strip(current));
      current.clear();
    } else {
      current += c;
    }
  }
  arguments.push_back(strip(current));
  return arguments;
}

/* Hand the argument text of each X(...) entry of a declaration X-macro to
 * use, one entry at a time: a bad entry fails before a later malformed one,
 * as the generator of the Python reader does. */
template <class Use> void x_entries(const macro &declaration, Use use) {
  const std::string callback =
      declaration.parameters.empty() ? "" : declaration.parameters[0];
  const std::string &body = declaration.body;
  const size_t end = rstrip(body).size();
  size_t position = 0;
  while (position < end) {
    size_t i = skip_space(body, position);
    if (body.compare(i, callback.size(), callback) != 0 ||
        (i = skip_space(body, i + callback.size())) >= body.size() ||
        body[i] != '(')
      reject(declaration.source, "JH-CFG-VARIANT",
             declaration.name + " must list only " + callback +
                 "(...) entries");
    const size_t first = i + 1;
    int depth = 1;
    size_t index = first;
    char quote = 0;
    while (index < body.size() && depth != 0) {
      const char c = body[index];
      if (quote != 0) {
        if (c == '\\')
          ++index;
        else if (c == quote)
          quote = 0;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '(') {
        ++depth;
      } else if (c == ')') {
        --depth;
      }
      ++index;
    }
    if (depth != 0)
      reject(declaration.source, "JH-CFG-VARIANT",
             "unbalanced " + callback + "(...) entry");
    use(body.substr(first, index - 1 - first));
    position = index;
  }
}

bool variant_id(const std::string &text) {
  if (text.empty() || !(text[0] >= 'A' && text[0] <= 'Z'))
    return false;
  return std::all_of(text.begin() + 1, text.end(), [](char c) {
    return (c >= 'A' && c <= 'Z') || ascii_digit(c) || c == '_';
  });
}

/* "(?:\\.|[^"\\])+" as a whole string. */
bool string_literal(const std::string &text) {
  if (text.size() < 3 || text.front() != '"')
    return false;
  size_t i = 1;
  while (i < text.size() && text[i] != '"')
    i += text[i] == '\\' ? (i + 1 < text.size() ? 2 : text.size()) : 1;
  return i == text.size() - 1;
}

/* NAME, or NAME = VALUE with spaces around "=" allowed. */
bool variant_definition(const std::string &text, std::string *name,
                        std::string *value, bool *has_value) {
  if (text.empty() || !name_start(text[0]))
    return false;
  size_t i = 1;
  while (i < text.size() && name_part(text[i]))
    ++i;
  *name = text.substr(0, i);
  *has_value = false;
  if (i == text.size())
    return true;
  i = skip_space(text, i);
  if (i >= text.size() || text[i] != '=')
    return false;
  *value = text.substr(skip_space(text, i + 1));
  *has_value = true;
  return true;
}

std::vector<variant> parse_variants(const macro *declaration) {
  std::vector<variant> variants;
  if (declaration == nullptr)
    return variants;
  if (!declaration->function_like || declaration->parameters.size() != 1)
    reject(declaration->source, "JH-CFG-VARIANT",
           std::string(variants_macro) +
               " must take one parameter, e.g. JH_PROJECT_VARIANTS(X)");
  x_entries(*declaration, [&](const std::string &entry) {
    const auto arguments = split_arguments(entry);
    if (arguments.size() < 3)
      reject(declaration->source, "JH-CFG-VARIANT",
             "a variant needs an id, a description and at least one "
             "definition: " +
                 python_repr(entry));
    const std::string &id = arguments[0];
    const std::string &description = arguments[1];
    const bool seen =
        std::any_of(variants.begin(), variants.end(),
                    [&](const variant &item) { return item.id == id; });
    if (!variant_id(id) || seen)
      reject(declaration->source, "JH-CFG-VARIANT",
             "variant id " + python_repr(id) +
                 " must be unique and use capital letters, digits and _, "
                 "starting with a letter");
    if (!string_literal(description))
      reject(declaration->source, "JH-CFG-VARIANT",
             "variant " + id + " needs a non-empty string description");
    variant result{id,
                   description.substr(1, description.size() - 2),
                   declaration->source,
                   {}};
    for (size_t i = 2; i < arguments.size(); ++i) {
      std::string name, value;
      bool has_value;
      if (!variant_definition(arguments[i], &name, &value, &has_value) ||
          is_target_selector(name))
        reject(declaration->source, "JH-CFG-VARIANT",
               "variant " + id + " has invalid definition " +
                   python_repr(arguments[i]));
      // The -D form: the compiler takes no spaces around "=".
      result.definitions.push_back(has_value ? name + "=" + value : name);
    }
    variants.push_back(std::move(result));
  });
  return variants;
}

std::vector<std::string> parse_targets(const macro *declaration) {
  std::vector<std::string> selectors;
  if (declaration == nullptr)
    return selectors;
  if (!declaration->function_like || declaration->parameters.size() != 1)
    reject(declaration->source, "JH-CFG-VARIANT",
           std::string(targets_macro) +
               " must take one parameter, e.g. JH_PROJECT_TARGETS(X)");
  x_entries(*declaration, [&](const std::string &entry) {
    const std::string selector = strip(entry);
    if (!is_target_selector(selector) || contains(selectors, selector))
      reject(declaration->source, "JH-CFG-VARIANT",
             std::string(targets_macro) + " entry " + python_repr(selector) +
                 " must be one HAL_TARGET_* selector, listed once");
    selectors.push_back(selector);
  });
  if (selectors.empty())
    reject(declaration->source, "JH-CFG-VARIANT",
           std::string(targets_macro) + " lists no target");
  return selectors;
}

/* _DEFINE of the Python reader: \s*NAME(\([^)]*\))?(.*)$ */
bool define_parts(const std::string &argument, std::string *name,
                  bool *function_like, std::string *parameters,
                  std::string *rest) {
  size_t i = skip_space(argument, 0);
  if (i >= argument.size() || !name_start(argument[i]))
    return false;
  const size_t start = i++;
  while (i < argument.size() && name_part(argument[i]))
    ++i;
  *name = argument.substr(start, i - start);
  *function_like = false;
  if (i < argument.size() && argument[i] == '(') {
    const size_t close = argument.find(')', i + 1);
    if (close != std::string::npos) {
      *function_like = true;
      *parameters = argument.substr(i + 1, close - i - 1);
      i = close + 1;
    }
  }
  *rest = argument.substr(i);
  return true;
}

macro parse_define(const std::string &argument, const std::string &source) {
  macro result;
  std::string parameters, rest;
  if (!define_parts(argument, &result.name, &result.function_like, &parameters,
                    &rest))
    reject(source, "JH-CFG-SCOPE", "malformed #define");
  if (result.function_like) {
    size_t begin = 0;
    while (true) {
      const size_t comma = parameters.find(',', begin);
      const std::string item = strip(parameters.substr(
          begin,
          comma == std::string::npos ? std::string::npos : comma - begin));
      if (!item.empty())
        result.parameters.push_back(item);
      if (comma == std::string::npos)
        break;
      begin = comma + 1;
    }
  }
  result.body = strip(rest);
  result.source = source;
  return result;
}

std::string include_path(const std::string &argument, const source_line &line) {
  if (argument.size() < 3 || argument.front() != '"' ||
      argument.back() != '"' || argument.find('"', 1) != argument.size() - 1)
    reject(line.where(), "JH-CFG-SCOPE",
           "only #include \"file\" relative to the project configuration is "
           "supported");
  return joined(parent_of(line.path), argument.substr(1, argument.size() - 2));
}

bool quoted_include(const std::string &argument) {
  return argument.size() >= 3 && argument.front() == '"' &&
         argument.back() == '"' && argument.find('"', 1) == argument.size() - 1;
}

// ── Reading ─────────────────────────────────────────────────────────────────

struct directive_line {
  std::string name, argument;
  source_line line;
};

std::vector<directive_line> directives_of(const std::string &path,
                                          const std::string &text) {
  std::vector<directive_line> result;
  for (const auto &item : logical_lines(text)) {
    directive_line directive{{}, {}, {path, item.number}};
    if (split_directive(item.text, directive.line, &directive.name,
                        &directive.argument))
      result.push_back(std::move(directive));
  }
  return result;
}

/* True when the directives wrap the whole file in a classic include guard. */
bool include_guard(const std::vector<directive_line> &directives) {
  if (directives.size() < 3 || directives[0].name != "ifndef")
    return false;
  std::string guard, parameters, rest;
  bool function_like;
  if (directives[1].name != "define" ||
      !define_parts(directives[1].argument, &guard, &function_like, &parameters,
                    &rest) ||
      guard != directives[0].argument)
    return false;
  int depth = 0;
  for (size_t index = 0; index < directives.size(); ++index) {
    const std::string &name = directives[index].name;
    if (name == "if" || name == "ifdef" || name == "ifndef") {
      ++depth;
    } else if (depth == 1 && (name == "elif" || name == "elifdef" ||
                              name == "elifndef" || name == "else")) {
      return false;
    } else if (name == "endif") {
      if (--depth == 0)
        return index == directives.size() - 1;
    }
  }
  return false;
}

struct scan_result {
  std::set<std::string> defined;
  std::map<std::string, macro> declarations;
};

/* Syntactic view of the header files, independent of any condition. */
void scan(const std::string &input, scan_result &result,
          std::set<std::string> &seen, size_t depth) {
  const std::string path = resolved(input);
  if (seen.count(path) != 0 || !regular_file(path))
    return;
  if (depth > max_macro_nesting)
    limit(path, "includes nest more than " + std::to_string(max_macro_nesting) +
                    " levels");
  seen.insert(path);
  const auto directives = directives_of(path, read_text(path));
  // Declarations inside the file's own include guard are unconditional.
  int nesting = include_guard(directives) ? -1 : 0;
  for (const auto &directive : directives) {
    const std::string &name = directive.name;
    if (name == "if" || name == "ifdef" || name == "ifndef") {
      ++nesting;
    } else if (name == "endif") {
      --nesting;
    } else if (name == "define" || name == "undef") {
      std::string declared, parameters, rest;
      bool function_like;
      if (!define_parts(directive.argument, &declared, &function_like,
                        &parameters, &rest))
        continue;
      result.defined.insert(declared);
      if (declared == variants_macro || declared == targets_macro) {
        if (name != "define" || nesting > 0 || depth != 0 ||
            result.declarations.count(declared) != 0)
          reject(directive.line.where(), "JH-CFG-VARIANT",
                 "define " + declared + " once, unconditionally, in " +
                     header_name);
        result.declarations[declared] =
            parse_define(directive.argument, directive.line.where());
      }
    } else if (name == "include" && quoted_include(directive.argument)) {
      scan(include_path(directive.argument, directive.line), result, seen,
           depth + 1);
    }
  }
}

struct frame {
  bool parent_active, active, taken;
  std::set<std::string> unknown;
};

class reader {
public:
  macro_table macros;
  std::set<std::string> known;
  std::vector<frame> stack;
  std::vector<std::string> files;
  std::set<std::string> once;
  std::map<std::string, std::set<std::string>> dependencies;
  bool stopped = false;
  std::string error_message, error_source;
  size_t max_work = 0, work_left = 0;

  bool active() const { return stack.empty() || stack.back().active; }

  void read(const std::string &input, size_t depth) {
    const std::string path = resolved(input);
    if (once.count(path) != 0 || stopped)
      return;
    if (depth > max_include_depth)
      reject(path, "JH-CFG-SCOPE", "includes nest too deeply");
    if (!contains(files, path))
      files.push_back(path);
    const std::string text = read_text(path);
    const size_t base_depth = stack.size();
    for (const auto &item : logical_lines(text)) {
      const source_line line{path, item.number};
      std::string name, argument;
      if (split_directive(item.text, line, &name, &argument))
        directive(name, argument, line, depth);
      if (stopped)
        return;
    }
    if (stack.size() != base_depth)
      reject(path, "JH-CFG-SCOPE", "unterminated #if");
  }

private:
  bool is_known(const std::string &name) const {
    return known.count(name) != 0 || feature_name(name);
  }

  /* Names the reader cannot see that decide text here, through the macros it
   * expands to and the conditions that defined them. */
  std::set<std::string> unknown_in(const std::string &text,
                                   const source_line &line) {
    std::set<std::string> names, unknown;
    work_budget budget{work_left, max_work};
    referenced_names(pp_names(text), macros, {}, names, line.where(), budget);
    for (const auto &name : names) {
      if (!is_known(name))
        unknown.insert(name);
      const auto found = dependencies.find(name);
      if (found != dependencies.end())
        unknown.insert(found->second.begin(), found->second.end());
    }
    return unknown;
  }

  /* Record what decides name here; an exact active definition replaces an
   * earlier uncertain one. */
  void note(const std::string &name) {
    std::set<std::string> unknown;
    for (const auto &item : stack)
      unknown.insert(item.unknown.begin(), item.unknown.end());
    if (!unknown.empty())
      dependencies[name].insert(unknown.begin(), unknown.end());
    else if (active())
      dependencies.erase(name);
  }

  static std::string tested(const std::string &kind,
                            const std::string &argument) {
    return kind == "ifdef" || kind == "ifndef" || kind == "elifdef" ||
                   kind == "elifndef"
               ? "defined " + argument
               : argument;
  }

  bool condition(const std::string &kind, const std::string &argument,
                 const source_line &line) {
    if (kind == "ifdef" || kind == "ifndef" || kind == "elifdef" ||
        kind == "elifndef") {
      if (!identifier(argument))
        reject(line.where(), "JH-CFG-SCOPE", "#" + kind + " needs one name");
      return (macros.find(argument) != nullptr) ==
             (kind == "ifdef" || kind == "elifdef");
    }
    try {
      work_budget budget{work_left, max_work};
      return evaluate(argument, macros, line.where(), budget) != 0;
    } catch (const range_error &error) {
      limit(line.where(),
            error.detail + " exceeds the signed 64-bit range of jh");
    }
  }

  frame &top(const std::string &name, const source_line &line) {
    if (stack.empty())
      reject(line.where(), "JH-CFG-SCOPE", "#" + name + " without #if");
    return stack.back();
  }

  void directive(const std::string &name, const std::string &argument,
                 const source_line &line, size_t depth) {
    if (name == "if" || name == "ifdef" || name == "ifndef") {
      const bool parent = active();
      const bool result = parent && condition(name, argument, line);
      stack.push_back(
          {parent, result, result, unknown_in(tested(name, argument), line)});
    } else if (name == "elif" || name == "elifdef" || name == "elifndef") {
      frame &current = top(name, line);
      const auto unknown = unknown_in(tested(name, argument), line);
      current.unknown.insert(unknown.begin(), unknown.end());
      const bool result = current.parent_active && !current.taken &&
                          condition(name, argument, line);
      current.active = result;
      current.taken = current.taken || result;
    } else if (name == "else") {
      frame &current = top(name, line);
      current.active = current.parent_active && !current.taken;
      current.taken = true;
    } else if (name == "endif") {
      top(name, line);
      stack.pop_back();
    } else if (name == "define") {
      macro definition = parse_define(argument, line.where());
      note(definition.name);
      if (active())
        macros.set(std::move(definition));
    } else if (name == "undef") {
      if (!identifier(argument))
        reject(line.where(), "JH-CFG-SCOPE", "#undef needs one name");
      note(argument);
      if (active())
        macros.erase(argument);
    } else if (!active()) {
      return;
    } else if (name == "include") {
      read(include_path(argument, line), depth + 1);
    } else if (name == "error") {
      stopped = true;
      error_message = argument;
      error_source = line.where();
    } else if (name == "pragma") {
      if (argument == "once")
        once.insert(line.path);
    } else if (name != "warning" && name != "line") {
      reject(line.where(), "JH-CFG-SCOPE", "unsupported #" + name);
    }
  }
};

macro_table predefined_macros(const std::vector<std::string> &definitions) {
  macro_table macros;
  for (const auto &definition : definitions) {
    const std::string text = strip(definition);
    size_t i = 0;
    if (text.empty() || !name_start(text[0]))
      reject("predefined", "JH-CFG-VALUE",
             "invalid definition " + python_repr(definition));
    while (i < text.size() && name_part(text[i]))
      ++i;
    if (i < text.size() && text[i] != '=')
      reject("predefined", "JH-CFG-VALUE",
             "invalid definition " + python_repr(definition));
    macro value;
    value.name = text.substr(0, i);
    value.body = i < text.size() ? strip(text.substr(i + 1)) : "1";
    value.source = "predefined";
    macros.set(std::move(value));
  }
  return macros;
}

/* #define/#undef of every file read, for the checks that look at any branch. */
std::vector<directive> file_directives(const std::vector<std::string> &files) {
  std::vector<directive> result;
  for (const auto &path : files) {
    for (const auto &item : directives_of(path, read_text(path))) {
      if (item.name != "define" && item.name != "undef")
        continue;
      std::string name, parameters, rest;
      bool function_like;
      if (!define_parts(item.argument, &name, &function_like, &parameters,
                        &rest))
        continue;
      const size_t after = skip_space(item.argument, 0) + name.size();
      result.push_back({path, name, strip(item.argument.substr(after)),
                        item.line.number, item.name == "undef"});
    }
  }
  return result;
}

/* unresolved() of definition (an object-like macro with a body), charged to
 * the budget of one query. */
std::set<std::string> unresolved_names(const project_config &config,
                                       const macro &definition,
                                       work_budget &budget) {
  std::set<std::string> names, result;
  referenced_names(pp_names(definition.body), config.macros, {definition.name},
                   names, definition.source, budget);
  for (const auto &item : names) {
    const macro *used = config.macros.find(item);
    if (used == nullptr || used->function_like)
      result.insert(item);
  }
  for (const auto &item : names) {
    const auto found = config.dependencies.find(item);
    if (found != config.dependencies.end())
      result.insert(found->second.begin(), found->second.end());
  }
  return result;
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

const macro *macro_table::find(const std::string &name) const {
  const auto found = index_.find(name);
  return found == index_.end() ? nullptr : &entries_[found->second];
}

void macro_table::set(macro value) {
  const auto found = index_.find(value.name);
  if (found != index_.end()) {
    entries_[found->second] = std::move(value);
    return;
  }
  index_.emplace(value.name, entries_.size());
  entries_.push_back(std::move(value));
  live_.push_back(true);
}

void macro_table::erase(const std::string &name) {
  const auto found = index_.find(name);
  if (found == index_.end())
    return;
  live_[found->second] = false;
  index_.erase(found);
}

std::vector<const macro *> macro_table::ordered() const {
  std::vector<const macro *> result;
  for (size_t i = 0; i < entries_.size(); ++i)
    if (live_[i])
      result.push_back(&entries_[i]);
  return result;
}

std::set<std::string> project_config::uncertain(const std::string &name) const {
  const auto found = dependencies.find(name);
  return found == dependencies.end() ? std::set<std::string>{} : found->second;
}

std::set<std::string>
project_config::unresolved(const std::string &name) const {
  const macro *definition = macros.find(name);
  if (definition == nullptr || definition->function_like ||
      definition->body.empty())
    return {};
  size_t left = max_work;
  work_budget budget{left, max_work};
  return unresolved_names(*this, *definition, budget);
}

integer_value project_config::integer(const std::string &name) const {
  return value(name).integer;
}

macro_value project_config::value(const std::string &name) const {
  macro_value result;
  const macro *definition = macros.find(name);
  if (definition == nullptr || definition->function_like ||
      definition->body.empty())
    return result;
  // One budget for the whole query, none shared with other queries.
  size_t left = max_work;
  work_budget budget{left, max_work};
  result.unresolved = unresolved_names(*this, *definition, budget);
  if (!result.unresolved.empty())
    return result;
  try {
    result.integer = {
        integer_kind::value,
        evaluate(definition->body, macros, definition->source, budget)};
  } catch (const range_error &) {
    result.integer = {integer_kind::out_of_range, 0};
  } catch (const failure &error) {
    // A body the reader cannot evaluate has no value; invalid literals and
    // the limits of jh still fail.
    if (error.status != HAL_ECONFIG)
      throw;
  }
  return result;
}

std::vector<const macro *> project_config::features() const {
  std::vector<const macro *> result;
  for (const macro *item : macros.ordered())
    if (feature_name(item->name) && item->source != "predefined")
      result.push_back(item);
  return result;
}

std::vector<directive> read_directives(const std::string &path) {
  if (!regular_file(path))
    return {};
  return file_directives({resolved(path)});
}

project_config read_project_config(const std::string &header,
                                   const std::vector<std::string> &predefined,
                                   const std::string &preinclude,
                                   const read_limits &limits) {
  project_config config;
  config.max_work = limits.max_work;
  macro_table macros = predefined_macros(predefined);
  std::set<std::string> early;
  if (!preinclude.empty()) {
    scan_result result;
    std::set<std::string> seen;
    scan(preinclude, result, seen, 0);
    early = std::move(result.defined);
  }
  std::set<std::string> predefined_names;
  for (const macro *item : macros.ordered())
    predefined_names.insert(item->name);
  reader state;
  state.max_work = state.work_left = limits.max_work;
  state.macros = std::move(macros);
  if (header.empty() || !regular_file(header)) {
    if (preinclude.empty()) {
      config.macros = std::move(state.macros);
      return config;
    }
    state.known = early;
    state.known.insert(predefined_names.begin(), predefined_names.end());
    state.read(preinclude, 0);
    config.macros = std::move(state.macros);
    config.files = std::move(state.files);
    config.directives = file_directives(config.files);
    config.stopped = state.stopped;
    config.error_message = std::move(state.error_message);
    config.error_source = std::move(state.error_source);
    return config;
  }
  scan_result result;
  std::set<std::string> seen;
  scan(header, result, seen, 0);
  const auto found_variants = result.declarations.find(variants_macro);
  config.variants = parse_variants(found_variants == result.declarations.end()
                                       ? nullptr
                                       : &found_variants->second);
  const auto found_targets = result.declarations.find(targets_macro);
  config.targets = parse_targets(found_targets == result.declarations.end()
                                     ? nullptr
                                     : &found_targets->second);
  state.known = std::move(result.defined);
  state.known.insert(early.begin(), early.end());
  state.known.insert(predefined_names.begin(), predefined_names.end());
  state.known.insert(target_selectors.begin(), target_selectors.end());
  for (const auto &item : config.variants)
    for (const auto &definition : item.definitions)
      state.known.insert(definition.substr(0, definition.find('=')));
  if (!preinclude.empty())
    state.read(preinclude, 0);
  state.read(header, 0);
  config.macros = std::move(state.macros);
  config.files = std::move(state.files);
  config.directives = file_directives(config.files);
  config.dependencies = std::move(state.dependencies);
  config.stopped = state.stopped;
  config.error_message = std::move(state.error_message);
  config.error_source = std::move(state.error_source);
  return config;
}

} // namespace jh::config
