#ifndef JH_EFFECTIVE_PROJECT_H
#define JH_EFFECTIVE_PROJECT_H

#include "jh_hardware_internal.h"

/* Configuration checks used by the effective lint after manifest validation.
 * Strings borrow h; findings use the caller's per-configuration failure. */
const char *jh_effective_target(jh_hw_t *h, jb_json_tree_view_t manifest,
                                jb_json_tree_view_t settings,
                                const char *target);
void jh_effective_identity(jh_hw_t *h, jb_json_tree_view_t manifest,
                           jh_failure_t *failure);
void jh_effective_sections(jh_hw_t *h, jb_json_tree_view_t manifest,
                           jb_json_tree_view_t settings, const char *target,
                           bool assembly, bool variant, jh_failure_t *failure);
void jh_effective_checkout(jh_hw_t *h, const char *directory,
                           jb_json_tree_view_t manifest,
                           jb_json_tree_view_t settings, const char *target,
                           const char *assembly, jh_failure_t *failure);
#endif
