#include "hal/hal_digipot.h"
#include "hal/hal_i2c.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

/* Datasheet anchors used by these tests:
 * - MCP401x: fixed I2C address, 7-bit wiper range (0..127), and mode limits
 *   by device variant (MCP4017/18/19), plus W-L/W-H/divider transfer behavior.
 * - MAX5395: valid addresses (0x28/0x29/0x2B), command set (RST, SD_CLR,
 *   SD_H_WREG/SD_L_WREG, QP_OFF, WIPER), and charge-pump dependent wiper
 *   resistance used in W-L/W-H conversion math.
 */

void setUp(void) {
    hal_i2c_init(0, 0, 100000);
    hal_mock_i2c_reset_write_log();
    hal_mock_i2c_set_busy(false);
}
void tearDown(void) {}

/* Fetch captured write frame @p idx; fail the test if it is missing. */
static int get_frame(int idx, uint8_t *buf, int max) {
    int n = hal_mock_i2c_get_write_frame(idx, buf, max);
    TEST_ASSERT_TRUE_MESSAGE(n >= 0, "expected write frame is missing");
    return n;
}

/* ── MCP401x ─────────────────────────────────────────────────────────────── */

void test_mcp401x_init_validation(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.i2c_bus = 0;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4017;

    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_UINT16(127, hal_digipot_step_count(h));
    TEST_ASSERT_EQUAL_UINT32(10000, hal_digipot_e2e_resistance(h));
    hal_digipot_deinit(h);

    /* Unsupported end-to-end resistance. */
    cfg.e2e_resistance = 7000;
    TEST_ASSERT_NULL(hal_digipot_init(&cfg));
    cfg.e2e_resistance = 10000;

    /* MCP4017 only supports the W-L rheostat mode. */
    cfg.mode = HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER;
    TEST_ASSERT_NULL(hal_digipot_init(&cfg));

    /* MCP4018 supports voltage-divider mode. */
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4018;
    h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);
    hal_digipot_deinit(h);
}

void test_mcp401x_set_resistance_wl(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4017;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* WL, e2e=10k, R=5000 -> (5000-150)/10000*127 = 61.595 -> round -> 62. */
    uint8_t expected = 62;
    hal_mock_i2c_inject_rx(&expected, 1); /* read-back returns the same byte */
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 5000));

    uint8_t f[4];
    int n = get_frame(0, f, sizeof(f));
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8(62, f[0]);

    /* Read-back mismatch -> false. */
    uint8_t wrong = 99;
    hal_mock_i2c_inject_rx(&wrong, 1);
    TEST_ASSERT_FALSE(hal_digipot_set_resistance(h, 5000));

    hal_digipot_deinit(h);
}

void test_mcp401x_set_resistance_voltage_divider(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4018;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* VD, e2e=10k, R=5000 -> 5000/10000*127 = 63.5 -> truncate -> 63. */
    uint8_t expected = 63;
    hal_mock_i2c_inject_rx(&expected, 1);
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 5000));

    uint8_t f[4];
    (void)get_frame(0, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(63, f[0]);

    hal_digipot_deinit(h);
}

void test_mcp401x_wh_mode_on_mcp4018_inverts_wiper_and_uses_fixed_address(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.i2c_bus = 0;
    cfg.i2c_addr = 0x55; /* should be ignored by MCP401x backend */
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4018;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* WH, e2e=10k, R=5000:
     * base WL = round(((5000-150)/10000)*127) = round(61.595) = 62,
     * WH inverts to 127-62 = 65. */
    uint8_t expected = 65u;
    hal_mock_i2c_inject_rx(&expected, 1);
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 5000));

    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(65u, f[0]);
    TEST_ASSERT_EQUAL_UINT8(0x2Fu, hal_mock_i2c_get_last_addr());

    hal_digipot_deinit(h);
}

void test_mcp401x_clamps_below_wiper_resistance_to_zero_code(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4017;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    uint8_t expected = 0u;
    hal_mock_i2c_inject_rx(&expected, 1);
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 0u));

    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0u, f[0]);

    hal_digipot_deinit(h);
}

void test_mcp401x_readback_failure_returns_false(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4017;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    hal_mock_i2c_set_busy(true); /* request_from/read fails via busy backend path */
    TEST_ASSERT_FALSE(hal_digipot_set_resistance(h, 5000));
    hal_mock_i2c_set_busy(false);

    hal_digipot_deinit(h);
}

void test_mcp401x_resistance_above_e2e_rejected(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4017;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_FALSE(hal_digipot_set_resistance(h, 20000));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_write_frame_count());

    hal_digipot_deinit(h);
}

/* ── MAX5395 ─────────────────────────────────────────────────────────────── */

void test_max5395_init_sequence(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.i2c_addr = 0x28;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.charge_pump_en = true;

    hal_mock_i2c_reset_write_log();
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_UINT16(255, hal_digipot_step_count(h));

    /* charge pump on -> no QP_OFF: frames are RST then SD_H_WREG. */
    TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
    uint8_t f[4];
    (void)get_frame(0, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(0xC0, f[0]); /* RST */
    (void)get_frame(1, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(0x90, f[0]); /* SD_H_WREG (open H for W-L mode) */

    hal_digipot_deinit(h);
}

void test_max5395_init_charge_pump_off(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.i2c_addr = 0x29;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER;
    cfg.charge_pump_en = false;

    hal_mock_i2c_reset_write_log();
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* VD + cp off: RST then QP_OFF, no shutdown-condition frame. */
    TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
    uint8_t f[4];
    (void)get_frame(1, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(0xA0, f[0]); /* QP_OFF */

    hal_digipot_deinit(h);
}

void test_max5395_init_validation(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER;

    cfg.i2c_addr = 0x30; /* not a valid MAX5395 address */
    TEST_ASSERT_NULL(hal_digipot_init(&cfg));

    cfg.i2c_addr = 0x28;
    cfg.e2e_resistance = 20000; /* unsupported resistance */
    TEST_ASSERT_NULL(hal_digipot_init(&cfg));
}

void test_max5395_set_resistance_voltage_divider(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.i2c_addr = 0x2B;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER;
    cfg.charge_pump_en = true;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* VD, e2e=10k, R=5000 -> 5000/10000*255 = 127.5 -> truncate -> 127. */
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 5000));

    /* Frame 0: SD_CLR; frame 1: WIPER(0x00, 127). No trailing shutdown in VD.
     */
    TEST_ASSERT_EQUAL_INT(2, hal_mock_i2c_get_write_frame_count());
    uint8_t f[4];
    (void)get_frame(0, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(0x80, f[0]); /* SD_CLR */
    (void)get_frame(1, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(0x00, f[0]); /* WIPER command */
    TEST_ASSERT_EQUAL_UINT8(127, f[1]);

    hal_digipot_deinit(h);
}

void test_max5395_set_resistance_wl_with_config_read(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.i2c_addr = 0x28;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.charge_pump_en = true;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* Config read returns QP bit set (0x80) -> wiper resistance 25 ohm.
     * WL, R=5000: (5000-25)/10000*255 = 126.86 -> truncate -> 126. */
    uint8_t config = 0x80;
    hal_mock_i2c_inject_rx(&config, 1);
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 5000));

    /* Frames: [0] config-read cmd 0x80, [1] SD_CLR, [2] WIPER, [3] SD_H_WREG.
     */
    uint8_t f[4];
    int n = get_frame(2, f, sizeof(f)); /* the WIPER frame */
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_UINT8(0x00, f[0]); /* WIPER command */
    TEST_ASSERT_EQUAL_UINT8(126, f[1]);
    (void)get_frame(3, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(0x90, f[0]); /* SD_H_WREG re-asserted */

    hal_digipot_deinit(h);
}

void test_max5395_set_resistance_wh_inverts_wiper(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.i2c_addr = 0x28;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH;
    cfg.charge_pump_en = true;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* Same maths as W-L (126) but W-H inverts: 255 - 126 = 129. */
    uint8_t config = 0x80;
    hal_mock_i2c_inject_rx(&config, 1);
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 5000));

    uint8_t f[4];
    (void)get_frame(2, f, sizeof(f)); /* WIPER frame */
    TEST_ASSERT_EQUAL_UINT8(0x00, f[0]);
    TEST_ASSERT_EQUAL_UINT8(129, f[1]);
    (void)get_frame(3, f, sizeof(f));
    TEST_ASSERT_EQUAL_UINT8(0x88, f[0]); /* SD_L_WREG (open L for W-H mode) */

    hal_digipot_deinit(h);
}

void test_max5395_variable_mode_config_read_failure_returns_false(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.i2c_addr = 0x28;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.charge_pump_en = true;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    hal_mock_i2c_set_busy(true); /* read cmd fails */
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_FALSE(hal_digipot_set_resistance(h, 5000));
    hal_mock_i2c_set_busy(false);

    /* Only config-read command frame should have been attempted. */
    TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count());
    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(1, get_frame(0, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x80u, f[0]);

    hal_digipot_deinit(h);
}

void test_max5395_wh_mode_uses_qp_off_wiper_resistance_from_config(void) {
    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MAX5395;
    cfg.i2c_addr = 0x28;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WH;
    cfg.charge_pump_en = false;
    hal_digipot_t h = hal_digipot_init(&cfg);
    TEST_ASSERT_NOT_NULL(h);

    /* QP bit clear => wiper resistance 45 ohm.
     * WL base: ((5000-45)/10000)*255 = 126.35 -> truncate 126.
     * WH invert: 255 - 126 = 129. */
    uint8_t config = 0x00u;
    hal_mock_i2c_inject_rx(&config, 1);
    hal_mock_i2c_reset_write_log();
    TEST_ASSERT_TRUE(hal_digipot_set_resistance(h, 5000));

    uint8_t f[4] = {};
    TEST_ASSERT_EQUAL_INT(2, get_frame(2, f, sizeof(f)));
    TEST_ASSERT_EQUAL_UINT8(0x00u, f[0]);
    TEST_ASSERT_EQUAL_UINT8(129u, f[1]);

    hal_digipot_deinit(h);
}

void test_public_api_guards_and_pool_limit(void) {
    TEST_ASSERT_FALSE(hal_digipot_set_resistance(NULL, 1000));
    TEST_ASSERT_EQUAL_UINT16(0u, hal_digipot_step_count(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_digipot_e2e_resistance(NULL));
    TEST_ASSERT_EQUAL_INT(HAL_DIGIPOT_MODE_VOLTAGE_DIVIDER, hal_digipot_mode(NULL));
    hal_digipot_deinit(NULL);

    hal_digipot_config_t cfg = {};
    cfg.chip = HAL_DIGIPOT_CHIP_MCP401X;
    cfg.e2e_resistance = 10000;
    cfg.mode = HAL_DIGIPOT_MODE_VARIABLE_RESISTOR_WL;
    cfg.mcp401x_device = HAL_DIGIPOT_MCP4017;

    hal_digipot_t hs[HAL_DIGIPOT_MAX_INSTANCES] = {};
    for (int i = 0; i < HAL_DIGIPOT_MAX_INSTANCES; ++i) {
        hs[i] = hal_digipot_init(&cfg);
        TEST_ASSERT_NOT_NULL(hs[i]);
    }
    TEST_ASSERT_NULL(hal_digipot_init(&cfg));
    for (int i = 0; i < HAL_DIGIPOT_MAX_INSTANCES; ++i) {
        hal_digipot_deinit(hs[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mcp401x_init_validation);
    RUN_TEST(test_mcp401x_set_resistance_wl);
    RUN_TEST(test_mcp401x_set_resistance_voltage_divider);
    RUN_TEST(test_mcp401x_wh_mode_on_mcp4018_inverts_wiper_and_uses_fixed_address);
    RUN_TEST(test_mcp401x_clamps_below_wiper_resistance_to_zero_code);
    RUN_TEST(test_mcp401x_readback_failure_returns_false);
    RUN_TEST(test_mcp401x_resistance_above_e2e_rejected);
    RUN_TEST(test_max5395_init_sequence);
    RUN_TEST(test_max5395_init_charge_pump_off);
    RUN_TEST(test_max5395_init_validation);
    RUN_TEST(test_max5395_set_resistance_voltage_divider);
    RUN_TEST(test_max5395_set_resistance_wl_with_config_read);
    RUN_TEST(test_max5395_set_resistance_wh_inverts_wiper);
    RUN_TEST(test_max5395_variable_mode_config_read_failure_returns_false);
    RUN_TEST(test_max5395_wh_mode_uses_qp_off_wiper_resistance_from_config);
    RUN_TEST(test_public_api_guards_and_pool_limit);
    return UNITY_END();
}
