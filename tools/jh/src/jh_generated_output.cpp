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

std::vector<std::string> split_lines(const std::string &text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    size_t stop = end;
    if (stop > start && text[stop - 1] == '\r')
      --stop;
    lines.emplace_back(text, start, stop - start);
    start = end + 1;
  }
  return lines;
}

/* Edit script between the two sequences: common prefix and suffix first,
 * then a longest common subsequence of the middle while its table stays
 * small; larger middles are reported as one removal and one insertion. */
std::vector<opcode> opcodes(const std::vector<std::string> &a,
                            const std::vector<std::string> &b) {
  size_t prefix = 0;
  while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
    ++prefix;
  size_t suffix = 0;
  while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
         a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix])
    ++suffix;
  const size_t n = a.size() - prefix - suffix, m = b.size() - prefix - suffix;
  std::vector<op> script;
  constexpr size_t table_limit = 4000000;
  if (n != 0 && m != 0 && m <= table_limit / n) {
    const size_t width = m + 1;
    std::vector<uint32_t> table((n + 1) * width, 0);
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
    script.insert(script.end(), n - i, op::remove);
    script.insert(script.end(), m - j, op::insert);
  } else {
    script.insert(script.end(), n, op::remove);
    script.insert(script.end(), m, op::insert);
  }
  std::vector<opcode> result;
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

std::string format_range(size_t start, size_t stop) {
  size_t beginning = start + 1;
  const size_t length = stop - start;
  if (length == 1)
    return std::to_string(beginning);
  if (length == 0)
    --beginning;
  return std::to_string(beginning) + "," + std::to_string(length);
}
} // namespace

std::vector<std::string>
unified_diff(const std::vector<std::string> &from_lines,
             const std::vector<std::string> &to_lines, const std::string &from,
             const std::string &to) {
  constexpr size_t context = 3;
  std::vector<opcode> codes = opcodes(from_lines, to_lines);
  std::vector<std::string> output;
  if (codes.empty())
    return output;
  if (codes.front().tag == op::equal) {
    opcode &first = codes.front();
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
  std::vector<std::vector<opcode>> groups;
  std::vector<opcode> group;
  for (opcode code : codes) {
    if (code.tag == op::equal && code.a_end - code.a_begin > 2 * context) {
      group.push_back(
          {code.tag, code.a_begin, std::min(code.a_end, code.a_begin + context),
           code.b_begin, std::min(code.b_end, code.b_begin + context)});
      groups.push_back(std::move(group));
      group.clear();
      code.a_begin = std::max(code.a_begin, code.a_end - context);
      code.b_begin = std::max(code.b_begin, code.b_end - context);
    }
    group.push_back(code);
  }
  if (!group.empty() && !(group.size() == 1 && group.front().tag == op::equal))
    groups.push_back(std::move(group));
  if (groups.empty())
    return output;
  output.push_back("--- " + from);
  output.push_back("+++ " + to);
  for (const auto &hunk : groups) {
    output.push_back(
        "@@ -" + format_range(hunk.front().a_begin, hunk.back().a_end) + " +" +
        format_range(hunk.front().b_begin, hunk.back().b_end) + " @@");
    for (const opcode &code : hunk) {
      if (code.tag == op::equal) {
        for (size_t i = code.a_begin; i < code.a_end; ++i)
          output.push_back(" " + from_lines[i]);
      } else if (code.tag == op::remove) {
        for (size_t i = code.a_begin; i < code.a_end; ++i)
          output.push_back("-" + from_lines[i]);
      } else {
        for (size_t j = code.b_begin; j < code.b_end; ++j)
          output.push_back("+" + to_lines[j]);
      }
    }
  }
  return output;
}

bool write_generated(const std::string &path, const std::string &content) {
  bool changed = false;
  require(jb_file_write_atomic(path.c_str(), content.data(), content.size(),
                               false, &changed),
          3, "cannot publish generated output");
  return changed;
}

bool check_generated(const std::string &path, const std::string &content,
                     size_t diff_limit, const std::string &label) {
  file_owner existing;
  const hal_status_t status =
      jb_file_read(path.c_str(), 64 * 1024 * 1024, &existing.data);
  if (status == HAL_ENOENT) {
    std::fprintf(stderr, "error: missing generated artifact %s\n",
                 path.c_str());
    return false;
  }
  require(status, status == HAL_EOVERFLOW || status == HAL_ENOMEM ? 4 : 3,
          "cannot read generated output");
  const std::string actual(reinterpret_cast<const char *>(existing.data.data),
                           existing.data.length);
  if (actual == content)
    return true;
  std::fprintf(stderr, "error: stale generated artifact %s\n", path.c_str());
  const auto lines =
      unified_diff(split_lines(actual), split_lines(content), path, label);
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
