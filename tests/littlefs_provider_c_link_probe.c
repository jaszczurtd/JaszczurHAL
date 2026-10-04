/* jh_littlefs_provider.h links from C, including the littlefs block
 * adapters; built into test_jh_littlefs_lfs_provider with the provider. */
#include "hal/storage/jh_littlefs_provider.h"

#include "c_link_probe.h"

bool jh_littlefs_provider_header_links_from_c(void);

bool jh_littlefs_provider_header_links_from_c(void) {
  static const volatile c_link_probe_fn_t k_functions[] = {
      C_LINK_PROBE_FN(jh_littlefs_lfs_provider_configure),
      C_LINK_PROBE_FN(jh_littlefs_block_read),
      C_LINK_PROBE_FN(jh_littlefs_block_program),
      C_LINK_PROBE_FN(jh_littlefs_block_erase),
      C_LINK_PROBE_FN(jh_littlefs_block_sync),
  };
  return C_LINK_PROBE_ALL(k_functions);
}
