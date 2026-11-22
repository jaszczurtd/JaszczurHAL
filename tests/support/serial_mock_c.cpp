#include "serial_mock_c.h"
#include "hal/impl/.mock/hal_mock.h"

void jh_test_serial_reset(void) { hal_mock_serial_reset(); }
void jh_test_serial_tx_limit(size_t limit) {
  hal_mock_serial_set_tx_limit(limit);
}
void jh_test_serial_inject(const char *text) {
  hal_mock_serial_inject_rx(text, -1);
}
void jh_test_bootloader_reset(void) { hal_mock_bootloader_reset_flag(); }
bool jh_test_bootloader_requested(void) {
  return hal_mock_bootloader_was_requested();
}
const char *jh_test_serial_output(void) { return hal_mock_serial_last_line(); }
const char *jh_test_serial_debug(void) { return hal_mock_deb_last_line(); }
void jh_test_set_millis(uint32_t ms) { hal_mock_set_millis(ms); }
