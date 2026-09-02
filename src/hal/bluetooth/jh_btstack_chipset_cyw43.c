/* Portable adaptation of Pico SDK's BSD-3-Clause CYW43 chipset adhesive. */
#include "jh_btstack_chipset_cyw43.h"

#include "btstack_util.h"

static void chipset_set_bd_addr_command(bd_addr_t addr,
                                        uint8_t *hci_cmd_buffer) {
  hci_cmd_buffer[0] = 0x01u;
  hci_cmd_buffer[1] = 0xfcu;
  hci_cmd_buffer[2] = 0x06u;
  reverse_bd_addr(addr, &hci_cmd_buffer[3]);
}

static const btstack_chipset_t s_chipset = {
    .name = "CYW43",
    .init = NULL,
    .next_command = NULL,
    .set_baudrate_command = NULL,
    .set_bd_addr_command = chipset_set_bd_addr_command,
};

const btstack_chipset_t *jh_btstack_chipset_cyw43_instance(void) {
  return &s_chipset;
}
