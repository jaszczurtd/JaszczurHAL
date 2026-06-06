#include "utils/unity.h"

#include "hal/hal_spi.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/display/st77xx_driver.h"

#include <string.h>

typedef struct {
    uint8_t commands[8];
    uint8_t arg_lens[8];
    uint8_t args[16];
    size_t command_count;
    size_t arg_count;
    uint32_t delays[4];
    size_t delay_count;
} command_recorder_t;

static bool record_command(void *ctx,
                           uint8_t command,
                           const uint8_t *data,
                           uint8_t data_len) {
    command_recorder_t *rec = (command_recorder_t *)ctx;
    if (rec->command_count < sizeof(rec->commands)) {
        rec->commands[rec->command_count] = command;
        rec->arg_lens[rec->command_count] = data_len;
    }
    rec->command_count++;

    for (uint8_t i = 0u; i < data_len && rec->arg_count < sizeof(rec->args); ++i) {
        rec->args[rec->arg_count++] = data[i];
    }
    return true;
}

static void record_delay(void *ctx, uint32_t delay_ms) {
    command_recorder_t *rec = (command_recorder_t *)ctx;
    if (rec->delay_count < sizeof(rec->delays) / sizeof(rec->delays[0])) {
        rec->delays[rec->delay_count] = delay_ms;
    }
    rec->delay_count++;
}

static jh_st77xx_config_t make_st7789_config(void) {
    jh_st77xx_config_t config = {};
    config.bus = 0u;
    config.cs_pin = 10;
    config.dc_pin = 11;
    config.rst_pin = -1;
    config.clock_hz = 32000000u;
    config.spi_mode = HAL_SPI_MODE0;
    config.chip = JH_ST77XX_CHIP_ST7789;
    config.width = 135u;
    config.height = 240u;
    return config;
}

static jh_st77xx_config_t make_st7796s_config(void) {
    jh_st77xx_config_t config = {};
    config.bus = 0u;
    config.cs_pin = 10;
    config.dc_pin = 11;
    config.rst_pin = -1;
    config.clock_hz = 32000000u;
    config.spi_mode = HAL_SPI_MODE0;
    config.chip = JH_ST77XX_CHIP_ST7796S;
    config.width = JH_ST7796S_TFTWIDTH;
    config.height = JH_ST7796S_TFTHEIGHT;
    config.bgr = true;
    return config;
}

void setUp(void) {
    hal_mock_spi_reset();
    hal_mock_set_millis(0u);
    hal_mock_set_micros(0u);
}

void tearDown(void) {}

static bool tx_has_tail(const uint8_t *tail, size_t tail_len) {
    uint8_t tx[512] = {};
    const size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
    if (tx_len < tail_len) {
        return false;
    }
    return memcmp(&tx[tx_len - tail_len], tail, tail_len) == 0;
}

void test_sequence_parser_handles_args_and_255_delay(void) {
    const uint8_t sequence[] = {
        2u,
        0xAAu, 2u, 0x11u, 0x22u,
        0xBBu, 0x80u, 255u
    };
    command_recorder_t rec = {};
    const jh_st77xx_command_io_t io = {
        &rec,
        record_command,
        record_delay
    };

    TEST_ASSERT_TRUE(jh_st77xx_run_sequence(&io, sequence));

    TEST_ASSERT_EQUAL_UINT(2u, rec.command_count);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, rec.commands[0]);
    TEST_ASSERT_EQUAL_UINT8(2u, rec.arg_lens[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11u, rec.args[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, rec.args[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBBu, rec.commands[1]);
    TEST_ASSERT_EQUAL_UINT(1u, rec.delay_count);
    TEST_ASSERT_EQUAL_UINT32(500u, rec.delays[0]);
}

void test_st7789_init_sets_offsets_and_rotation(void) {
    jh_st77xx_t dev = {};
    const jh_st77xx_config_t config = make_st7789_config();

    TEST_ASSERT_TRUE(jh_st77xx_init(&dev, &config));

    TEST_ASSERT_TRUE(dev.initialized);
    TEST_ASSERT_EQUAL_UINT16(135u, dev.width);
    TEST_ASSERT_EQUAL_UINT16(240u, dev.height);
    TEST_ASSERT_EQUAL_UINT8(53u, dev.x_start);
    TEST_ASSERT_EQUAL_UINT8(40u, dev.y_start);
    TEST_ASSERT_TRUE(hal_mock_gpio_is_output((uint8_t)config.cs_pin));
    TEST_ASSERT_TRUE(hal_mock_gpio_is_output((uint8_t)config.dc_pin));
    TEST_ASSERT_EQUAL_UINT32(config.clock_hz, hal_mock_spi_get_clock_hz(config.bus));
    TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(config.bus));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(config.bus));

    hal_mock_spi_reset();
    TEST_ASSERT_TRUE(jh_st77xx_set_rotation(&dev, 1u));

    const uint8_t tail[] = {0x36u, 0xA0u};
    TEST_ASSERT_TRUE(tx_has_tail(tail, sizeof(tail)));
    TEST_ASSERT_EQUAL_UINT16(240u, dev.width);
    TEST_ASSERT_EQUAL_UINT16(135u, dev.height);
    TEST_ASSERT_EQUAL_UINT8(40u, dev.x_start);
    TEST_ASSERT_EQUAL_UINT8(52u, dev.y_start);
}

void test_st7789_addr_window_applies_rotation_offsets_and_big_endian_pixels(void) {
    jh_st77xx_t dev = {};
    const jh_st77xx_config_t config = make_st7789_config();
    const uint16_t pixels[] = {0x1234u, 0xABCDu};
    TEST_ASSERT_TRUE(jh_st77xx_init(&dev, &config));
    TEST_ASSERT_TRUE(jh_st77xx_set_rotation(&dev, 1u));

    hal_mock_spi_reset();
    TEST_ASSERT_TRUE(jh_st77xx_draw_rgb_bitmap(&dev, 1u, 2u, pixels, 2u, 1u));

    const uint8_t tail[] = {
        0x2Au, 0x00u, 0x29u, 0x00u, 0x2Au,
        0x2Bu, 0x00u, 0x36u, 0x00u, 0x36u,
        0x2Cu,
        0x12u, 0x34u, 0xABu, 0xCDu
    };
    TEST_ASSERT_TRUE(tx_has_tail(tail, sizeof(tail)));
}

void test_st7796s_uses_swapped_invert_commands(void) {
    jh_st77xx_t dev = {};
    const jh_st77xx_config_t config = make_st7796s_config();
    TEST_ASSERT_TRUE(jh_st77xx_init(&dev, &config));

    hal_mock_spi_reset();
    TEST_ASSERT_TRUE(jh_st77xx_invert(&dev, true));
    const uint8_t invert_on_tail[] = {0x20u};
    TEST_ASSERT_TRUE(tx_has_tail(invert_on_tail, sizeof(invert_on_tail)));

    hal_mock_spi_reset();
    TEST_ASSERT_TRUE(jh_st77xx_invert(&dev, false));
    const uint8_t invert_off_tail[] = {0x21u};
    TEST_ASSERT_TRUE(tx_has_tail(invert_off_tail, sizeof(invert_off_tail)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sequence_parser_handles_args_and_255_delay);
    RUN_TEST(test_st7789_init_sets_offsets_and_rotation);
    RUN_TEST(test_st7789_addr_window_applies_rotation_offsets_and_big_endian_pixels);
    RUN_TEST(test_st7796s_uses_swapped_invert_commands);
    return UNITY_END();
}
