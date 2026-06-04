/**
 * @file main.c
 * @brief STM32G474 (Nucleo-G474RE) I2C bus scanner - hardware verification of
 *        the real hal_i2c backend.
 *
 * Probes every 7-bit address 0x08..0x77 and prints those that ACK. This is the
 * simplest way to prove the bare-metal I2C1 master works on real silicon.
 *
 * Wiring (I2C1): SCL = PB8, SDA = PB9, plus pull-ups to 3V3. Console on USART2
 * (ST-Link VCP) @ 115200. See README.md.
 */

#include <hal/hal_serial.h>
#include <hal/hal_system.h>
#include <hal/hal_i2c.h>

static void print_hex8(uint8_t v) {
    static const char hx[] = "0123456789ABCDEF";
    char s[3] = { hx[(v >> 4) & 0xF], hx[v & 0xF], '\0' };
    hal_serial_print(s);
}

int main(void) {
    hal_serial_begin(115200);
    hal_serial_println("");
    hal_serial_println("=== JaszczurHAL G474 I2C scanner ===");
    hal_serial_println("I2C1: SCL=PB8, SDA=PB9 (external pull-ups to 3V3 required)");

    hal_i2c_init(0, 0, HAL_I2C_CLOCK_STANDARD_HZ);

    for (;;) {
        hal_serial_println("scanning 0x08..0x77 ...");
        int found = 0;
        for (uint8_t addr = 0x08u; addr <= 0x77u; ++addr) {
            /* hal_i2c_is_busy() returns false when the device ACKs (present). */
            if (!hal_i2c_is_busy(addr)) {
                hal_serial_print("  device @ 0x");
                print_hex8(addr);
                hal_serial_println("");
                ++found;
            }
        }
        if (found == 0) {
            hal_serial_println("  (no devices found)");
        }
        hal_delay_ms(2000);
    }
}
