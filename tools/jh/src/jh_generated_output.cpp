#include "jh_generated_output.h"
#include "../json/jh_json_file.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace jh {

namespace {
enum class op { equal, remove, insert };
struct opcode {
  op tag;
  size_t a_begin, a_end, b_begin, b_end;
};

/* Lines of text without their terminators, borrowing its bytes. */
owned_array<std::string_view> split_lines(std::string_view text) {
  owned_array<std::string_view> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos)
      end = text.size();
    size_t stop = end;
    if (stop > start && text[stop - 1] == '\r')
      --stop;
    lines.push_back(text.substr(start, stop - start));
    start = end + 1;
  }
  return lines;
}

void repeat(owned_array<op> &script, op tag, size_t count) {
  for (size_t index = 0; index < count; ++index)
    script.push_back(tag);
}

/* Edit script between the two sequences: common prefix and suffix first,
 * then a longest common subsequence of the middle while its table stays
 * small; larger middles are reported as one removal and one insertion. */
owned_array<opcode> opcodes(const owned_array<std::string_view> &a,
                            const owned_array<std::string_view> &b) {
  size_t prefix = 0;
  while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
    ++prefix;
  size_t suffix = 0;
  while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
         a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
    ++suffix;
  const size_t n = a.size() - prefix - suffix, m = b.size() - prefix - suffix;
  owned_array<op> script;
  constexpr size_t table_limit = 4000000;
  if (n != 0 && m != 0 && m <= table_limit / n) {
    const size_t width = m + 1;
    owned_array<uint32_t> table;
    table.resize((n + 1) * width);
    for (size_t i = n; i-- > 0;)
      for (size_t j = m; j-- > 0;) {
        const uint32_t down = table[(i + 1) * width + j];
        const uint32_t right = table[i * width + j + 1];
        table[i * width + j] = a[prefix + i] == b[prefix + j]
                                   ? table[(i + 1) * width + j + 1] + 1
                                   : std::max(down, right);
      }
    size_t i = 0, j = 0;
    while (i < n && j < m) {
      if (a[prefix + i] == b[prefix + j]) {
        script.push_back(op::equal);
        ++i;
        ++j;
      } else if (table[(i + 1) * width + j] >= table[i * width + j + 1]) {
        script.push_back(op::remove);
        ++i;
      } else {
        script.push_back(op::insert);
        ++j;
      }
    }
    repeat(script, op::remove, n - i);
    repeat(script, op::insert, m - j);
  } else {
    repeat(script, op::remove, n);
    repeat(script, op::insert, m);
  }
  owned_array<opcode> result;
  const auto append = [&](op tag, size_t a_count, size_t b_count) {
    if (!result.empty() && result.back().tag == tag) {
      result.back().a_end += a_count;
      result.back().b_end += b_count;
      return;
    }
    const size_t a_at = result.empty() ? 0 : result.back().a_end;
    const size_t b_at = result.empty() ? 0 : result.back().b_end;
    result.push_back({tag, a_at, a_at + a_count, b_at, b_at + b_count});
  };
  if (prefix != 0)
    append(op::equal, prefix, prefix);
  for (const op tag : script)
    append(tag, tag == op::insert ? 0 : 1, tag == op::remove ? 0 : 1);
  if (suffix != 0)
    append(op::equal, suffix, suffix);
  return result;
}

/* A hunk range as difflib writes it, in a buffer of the call. */
struct range_text {
  char bytes[48];
  size_t length;
  std::string_view view() const { return {bytes, length}; }
};

range_text format_range(size_t start, size_t stop) {
  size_t beginning = start + 1;
  const size_t length = stop - start;
  range_text out{};
  int written;
  if (length == 1) {
    written = std::snprintf(out.bytes, sizeof out.bytes, "%zu", beginning);
  } else {
    if (length == 0)
      --beginning;
    written = std::snprintf(out.bytes, sizeof out.bytes, "%zu,%zu", beginning,
                            length);
  }
  out.length = written > 0 ? static_cast<size_t>(written) : 0;
  return out;
}

owned_string joined(std::string_view head, std::string_view tail) {
  owned_string out(head);
  out.append(tail);
  return out;
}
} // namespace

owned_list<owned_string>
unified_diff(const owned_array<std::string_view> &from_lines,
             const owned_array<std::string_view> &to_lines,
             std::string_view from, std::string_view to) {
  constexpr size_t context = 3;
  owned_array<opcode> codes = opcodes(from_lines, to_lines);
  owned_list<owned_string> output;
  if (codes.empty())
    return output;
  if (codes[0].tag == op::equal) {
    opcode &first = codes[0];
    first.a_begin =
        std::max(first.a_begin, first.a_end - std::min(first.a_end, context));
    first.b_begin =
        std::max(first.b_begin, first.b_end - std::min(first.b_end, context));
  }
  if (codes.back().tag == op::equal) {
    opcode &last = codes.back();
    last.a_end = std::min(last.a_end, last.a_begin + context);
    last.b_end = std::min(last.b_end, last.b_begin + context);
  }
  owned_list<owned_array<opcode>> groups;
  owned_array<opcode> group;
  for (opcode code : codes) {
    if (code.tag == op::equal && code.a_end - code.a_begin > 2 * context) {
      group.push_back(
          {code.tag, code.a_begin, std::min(code.a_end, code.a_begin + context),
           code.b_begin, std::min(code.b_end, code.b_begin + context)});
      // The moved-from group is empty again.
      groups.emplace_back(std::move(group));
      code.a_begin = std::max(code.a_begin, code.a_end - context);
      code.b_begin = std::max(code.b_begin, code.b_end - context);
    }
    group.push_back(code);
  }
  if (!group.empty() && !(group.size() == 1 && group[0].tag == op::equal))
    groups.emplace_back(std::move(group));
  if (groups.empty())
    return output;
  output.emplace_back(joined("--- ", from));
  output.emplace_back(joined("+++ ", to));
  for (size_t index = 0; index < groups.size(); ++index) {
    const owned_array<opcode> &hunk = groups[index];
    const opcode &first = hunk[0];
    const opcode &last = hunk[hunk.size() - 1];
    owned_string header("@@ -");
    header.append(format_range(first.a_begin, last.a_end).view());
    header.append(" +");
    header.append(format_range(first.b_begin, last.b_end).view());
    header.append(" @@");
    output.emplace_back(std::move(header));
    for (const opcode &code : hunk) {
      if (code.tag == op::equal) {
        for (size_t i = code.a_begin; i < code.a_end; ++i)
          output.emplace_back(joined(" ", from_lines[i]));
      } else if (code.tag == op::remove) {
        for (size_t i = code.a_begin; i < code.a_end; ++i)
          output.emplace_back(joined("-", from_lines[i]));
      } else {
        for (size_t j = code.b_begin; j < code.b_end; ++j)
          output.emplace_back(joined("+", to_lines[j]));
      }
    }
  }
  return output;
}

bool write_generated(const char *path, std::string_view content) {
  bool changed = false;
  require(jb_file_write_atomic(path, content.data(), content.size(), false,
                               &changed),
          3, "cannot publish generated output");
  return changed;
}

bool check_generated(const char *path, std::string_view content,
                     size_t diff_limit, std::string_view label) {
  file_owner existing;
  const hal_status_t status =
      jb_file_read(path, 64 * 1024 * 1024, &existing.data);
  if (status == HAL_ENOENT) {
    std::fprintf(stderr, "error: missing generated artifact %s\n", path);
    return false;
  }
  require(status, status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : 3,
          "cannot read generated output");
  const std::string_view actual(
      reinterpret_cast<const char *>(existing.data.data), existing.data.length);
  if (actual == content)
    return true;
  std::fprintf(stderr, "error: stale generated artifact %s\n", path);
  const auto from_lines = split_lines(actual);
  const auto to_lines = split_lines(content);
  const auto lines = unified_diff(from_lines, to_lines, path, label);
  for (size_t index = 0; index < lines.size(); ++index) {
    if (index == diff_limit) {
      std::fputs("... diff truncated ...\n", stderr);
      break;
    }
    std::fprintf(stderr, "%s\n", lines[index].c_str());
  }
  return false;
}

} // namespace jh
