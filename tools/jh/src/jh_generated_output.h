#ifndef JH_GENERATED_OUTPUT_H
#define JH_GENERATED_OUTPUT_H

/** @file jh_generated_output.h
 * Publishing and checking one generated artifact: atomic replacement that
 * keeps an identical file, or a comparison that changes nothing and prints a
 * bounded unified diff of the drift.
 */
#include <string>
#include <vector>

namespace jh {

/** Publish content at path atomically; returns whether the file changed. */
bool write_generated(const std::string &path, const std::string &content);

/** Compare content with the file at path without changing anything. A missing
 * or different file is reported on stderr, the difference as a unified diff
 * of at most diff_limit lines labelled path and label, and false is returned.
 */
bool check_generated(const std::string &path, const std::string &content,
                     size_t diff_limit, const std::string &label);

/** Unified diff lines (context 3) between two line sequences, without line
 * terminators, headed by --- from and +++ to. Equal inputs give no lines.
 */
std::vector<std::string>
unified_diff(const std::vector<std::string> &from_lines,
             const std::vector<std::string> &to_lines, const std::string &from,
             const std::string &to);

} // namespace jh
#endif
