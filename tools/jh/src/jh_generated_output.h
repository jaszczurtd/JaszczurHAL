#ifndef JH_GENERATED_OUTPUT_H
#define JH_GENERATED_OUTPUT_H

/** @file jh_generated_output.h
 * Publishing and checking one generated artifact: atomic replacement that
 * keeps an identical file, or a comparison that changes nothing and prints a
 * bounded unified diff of the drift.
 */
#include "jh_containers.h"
#include <cstddef>
#include <string_view>

namespace jh {

/** Publish content at path atomically; returns whether the file changed. */
bool write_generated(const char *path, std::string_view content);

/** Compare content with the file at path without changing anything. A missing
 * or different file is reported on stderr, the difference as a unified diff
 * of at most diff_limit lines labelled path and label, and false is returned.
 */
bool check_generated(const char *path, std::string_view content,
                     size_t diff_limit, std::string_view label);

/** Unified diff lines (context 3) between two line sequences, without line
 * terminators, headed by --- from and +++ to. Equal inputs give no lines.
 * The lines borrow their text, which must outlive the call.
 */
owned_list<owned_string>
unified_diff(const owned_array<std::string_view> &from_lines,
             const owned_array<std::string_view> &to_lines,
             std::string_view from, std::string_view to);

} // namespace jh
#endif
