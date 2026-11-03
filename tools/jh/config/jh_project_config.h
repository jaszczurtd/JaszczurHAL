#ifndef JH_PROJECT_CONFIG_H
#define JH_PROJECT_CONFIG_H

/** @file jh_project_config.h
 * Reader of a project's hal_project_config.h, ported from
 * scripts/project_config.py: it evaluates the header the way the C
 * preprocessor does for one set of -D definitions and reports the macros,
 * variants, targets, files read, an #error and the names it cannot see.
 *
 * Integer arithmetic is signed int64_t. A literal or a result outside that
 * range, in an operand the condition computes, fails with [JH-CFG-SCOPE]
 * where the Python reader computes an unbounded value; a macro value outside
 * the range has no integer (integer_kind::out_of_range). Operands C does not
 * compute (after a decided && or ||, the ?: branch not taken) are parsed but
 * not computed, as in Python. Non-ASCII letters and digits are not part of
 * numbers and names, as in C. C++17; failures throw jh::failure.
 */
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace jh::config {

/** One #define, or a -D definition with source "predefined". */
struct macro {
  std::string name, body, source;
  /** Parameter names of a function-like macro, empty entries removed. */
  std::vector<std::string> parameters;
  bool function_like = false;
};

/** One #define or #undef of a file the reader read, in any branch. */
struct directive {
  std::string file, name;
  /** For #define, the text after the name: parameters and body. */
  std::string text;
  size_t line = 0;
  bool undef = false;
};

/** One entry of JH_PROJECT_VARIANTS(X): definitions use the -D form. */
struct variant {
  std::string id, description, source;
  std::vector<std::string> definitions;
};

/** Macros in definition order: a redefinition keeps the place of the first
 * definition, #undef removes the name and a later #define appends it.
 */
class macro_table {
  std::vector<macro> entries_;
  std::vector<bool> live_;
  std::unordered_map<std::string, size_t> index_;

public:
  const macro *find(const std::string &name) const;
  void set(macro value);
  void erase(const std::string &name);
  /** Defined macros in order. */
  std::vector<const macro *> ordered() const;
};

enum class integer_kind { value, none, out_of_range };

/** Integer value of a macro: a value, none (not an integer here), or a value
 * the int64_t arithmetic of jh cannot represent.
 */
struct integer_value {
  integer_kind kind = integer_kind::none;
  int64_t value = 0;
};

/** What the build reads of one macro: the names its value uses that the
 * configuration cannot resolve, and its integer value.
 */
struct macro_value {
  std::set<std::string> unresolved;
  integer_value integer;
};

/** Work limit of one read: the tokens that expanding conditions and tracking
 * dependencies visit together. Each query on its result (unresolved(),
 * integer(), value()) has a budget of the same size of its own, so a query
 * never pays for another one. Exceeding a budget fails with HAL_EOVERFLOW
 * (exit 4).
 */
struct read_limits {
  size_t max_work = 1000000;
};

/** Result of evaluating the header for one set of predefined macros. */
struct project_config {
  macro_table macros;
  std::vector<variant> variants;
  /** Files read, resolved, in first-read order. */
  std::vector<std::string> files;
  /** HAL_TARGET_* selectors of JH_PROJECT_TARGETS(X). */
  std::vector<std::string> targets;
  /** #define/#undef of every file in files, active or not, in file order;
   * the whole file, also past an #error. */
  std::vector<directive> directives;
  /** Unknown identifiers that decide whether or how a name is defined. */
  std::map<std::string, std::set<std::string>> dependencies;
  /** The header stopped on #error: its text and place. */
  bool stopped = false;
  std::string error_message, error_source;
  /** Work budget of each unresolved(), integer() or value() call; set by
   * read_project_config().
   */
  size_t max_work = 0;

  /** Identifiers deciding name that the reader cannot see. */
  std::set<std::string> uncertain(const std::string &name) const;
  /** Names the value of name uses that this configuration cannot resolve. */
  std::set<std::string> unresolved(const std::string &name) const;
  /** Integer value of an object-like macro, as the build reads it. */
  integer_value integer(const std::string &name) const;
  /** unresolved() and integer() of one macro, resolving its names once. */
  macro_value value(const std::string &name) const;
  /** HAL feature definitions made by the header itself. */
  std::vector<const macro *> features() const;
};

/** Evaluate header (empty: none) with -D definitions (NAME or NAME=VALUE).
 * preinclude (empty: none) is read first, as the hook loads jh_hardware.h
 * before the project header. A missing header is an empty configuration.
 * Throws jh::failure: exit code 2 for a header the reader rejects
 * (HAL_ECONFIG, HAL_EPROTO for invalid UTF-8 or literals), 3 for a file it
 * cannot read and 4 for the int64_t range or a jh limit (HAL_EOVERFLOW). Each
 * macro is scanned once per dependency question, so the work grows with the
 * size of the header, not with the number of paths between its macros.
 */
/** #define/#undef of one file, active or not, without evaluating it; empty for
 * a path that is no regular file. Throws jh::failure like
 * read_project_config() for a file it cannot read.
 */
std::vector<directive> read_directives(const std::string &path);

project_config read_project_config(const std::string &header,
                                   const std::vector<std::string> &predefined,
                                   const std::string &preinclude,
                                   const read_limits &limits = {});

} // namespace jh::config
#endif
