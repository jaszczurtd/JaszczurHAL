#ifndef JH_BUILD_SUPPORT_H
#define JH_BUILD_SUPPORT_H

/** @file jh_build_support.h
 * Helpers the library and ESP-IDF builds share: path containment with the
 * separators and case rules of the host, text of registry descriptors and
 * the exit code a child process passes on. C11. */
#include "jh_failure.h"
#include <jb/json_tree.h>
#include <jb/process.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True for a path separator of the host: / on POSIX, / and \ on Windows. */
bool jh_build_separator(char c);

/** Length of the prefix of path that names parent, or SIZE_MAX when path
 * lies outside parent. Both paths are absolute and normalized; trailing
 * separators of parent are ignored. Windows compares case-insensitively and
 * treats / and \ alike. */
size_t jh_build_prefix_length(const char *path, const char *parent);

/** True when path is parent or lies below it (jh_build_prefix_length). */
bool jh_build_inside(const char *path, const char *parent);

/** The string at root.a.b of a descriptor (a and b may be NULL to stop
 * earlier), or an empty span when it is missing or not a string. */
jb_json_span_t jh_build_descriptor_text(jb_json_tree_view_t root, const char *a,
                                        const char *b);

/** The exit code a build passes on for a child that ran: 128 plus the
 * signal of a killed POSIX process, 1 for a code above 255, otherwise the
 * child's code. */
int jh_build_exit_code(const jb_process_result_t *result);

#ifdef __cplusplus
}
#endif
#endif
