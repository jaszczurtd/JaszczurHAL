#ifndef JH_PROCESS_SUPPORT_H
#define JH_PROCESS_SUPPORT_H

/** @file jh_process_support.h
 * UTF-8 host paths for compiler and device-tool processes. */
#include "jh_failure.h"
#include <jb/file.h>

#ifdef __cplusplus
extern "C" {
#endif
/** Copy a process environment value as UTF-8. found distinguishes an unset
 * variable from an empty one. name is an ASCII environment key. */
hal_status_t jh_process_environment(const char *name, jb_string_t *out,
                                    bool *found);
/** Resolve the home used by Python expanduser. Empty user means the current
 * user; unknown users leave found false. The output uses UTF-8. */
hal_status_t jh_process_user_home(const char *user, jb_string_t *out,
                                  bool *found, jh_failure_t *failure);
/** Find an executable by its absolute/relative path or its name in PATH.
 * base is the absolute working directory. out receives an absolute path
 * without resolving symlinks, preserving executable names used by wrappers;
 * Windows appends .exe to a bare name without that suffix. With
 * absolute_entries_only, preserve explicit paths, search only absolute PATH
 * entries, and retain an unfound bare name for the device runner. Otherwise
 * relative entries use base and missing names return HAL_ENOENT. On POSIX
 * the unrestricted search skips files without execute permission. Inaccessible
 * PATH entries, including symlink loops, are skipped. */
hal_status_t jh_process_executable(const char *name, const char *base,
                                   bool absolute_entries_only, jb_string_t *out,
                                   jh_failure_t *failure);
/** Create an owned scratch directory beneath the OS temporary directory.
 * POSIX falls back to /tmp if TMPDIR cannot hold a temporary directory.
 * out must point to NULL. Release with jb_temp_dir_remove. */
hal_status_t jh_process_scratch(jb_temp_dir_t **out, jh_failure_t *failure);
#ifdef __cplusplus
}
#endif
#endif
