/**
 * @file app.c
 * @brief Portable MCP2515 CAN example over JaszczurHAL SPI + CAN abstractions.
 */

#include <hal/hal_app.h>
#include <hal/hal_can.h>
#include <hal/hal_serial.h>
#include <hal/hal_spi.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK  18u
#define EXAMPLE_CAN_CS   17u
#else
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK  5u
#define EXAMPLE_CAN_CS   4u
#endif

static hal_can_t s_can = NULL;
static uint32_t s_counter = 0u;

static void print_uint32(uint32_t v) {
    char buf[11];
    int i = (int)sizeof(buf) - 1;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0);
    hal_serial_print(&buf[i]);
}

void app_start(void) {
    hal_serial_begin(115200);
    hal_serial_println("");
    hal_serial_println("=== JaszczurHAL MCP2515 CAN example ===");
    hal_serial_println("Initialising SPI bus and MCP2515 on CS pin...");

    hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);
    s_can = hal_can_create_with_retry(EXAMPLE_CAN_CS,
                                      HAL_CAN_NO_INT_PIN,
                                      NULL,
                                      2,
                                      NULL);
    if (s_can) {
        hal_serial_println("MCP2515 init OK");
    } else {
        hal_serial_println("MCP2515 init FAILED");
    }
}

void app_task0(void) {
    if (!s_can) {
        hal_delay_ms(1000);
        return;
    }

    uint8_t payload[8] = {0};
    payload[0] = (uint8_t)(s_counter & 0xFFu);
    payload[1] = (uint8_t)((s_counter >> 8) & 0xFFu);

    hal_serial_print("TX id=0x321 seq=");
    print_uint32(s_counter);
    if (hal_can_send(s_can, 0x321u, 2u, payload)) {
        hal_serial_println(" OK");
    } else {
        hal_serial_println(" FAIL");
    }

    while (hal_can_available(s_can)) {
        uint32_t id = 0u;
        uint8_t len = 0u;
        uint8_t rx[HAL_CAN_MAX_DATA_LEN] = {0};
        if (!hal_can_receive(s_can, &id, &len, rx)) {
            break;
        }
        hal_serial_print("RX id=");
        print_uint32(id);
        hal_serial_print(" len=");
        print_uint32(len);
        hal_serial_println("");
    }

    s_counter++;
    hal_delay_ms(1000);
}