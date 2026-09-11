#include "hal/core/hal_array.h"
#include "hal/i2c/hal_i2c_slave.h"
#include "utils/unity.h"
#include <cstring>
#include <hardware/i2c.h>
#include <hardware/irq.h>

// Exercise the production IRQ handler through a small peripheral fake.
i2c_inst_t jh_test_i2c[2];
irq_handler_t jh_test_i2c_irqs[2];
unsigned int jh_test_i2c_lock_depth;

extern "C" void hal_rp2040_serial_write_assert_fail(const char *text) {
  TEST_FAIL_MESSAGE(text);
}

void setUp(void) {
  hal_i2c_slave_deinit();
  hal_i2c_slave_deinit_bus(1U);
  memset(jh_test_i2c, 0, sizeof(jh_test_i2c));
  hal_i2c_slave_init(4, 5, 0x57);
  hal_i2c_slave_init_bus(1, 6, 7, 0x58);
}
void tearDown(void) { TEST_ASSERT_EQUAL_UINT32(0U, jh_test_i2c_lock_depth); }

static void irq(uint8_t bus, uint32_t flags) {
  jh_test_i2c[bus].hw.intr_stat = flags;
  TEST_ASSERT_NOT_NULL(jh_test_i2c_irqs[bus]);
  jh_test_i2c_irqs[bus]();
  TEST_ASSERT_EQUAL_UINT32(0U, jh_test_i2c_lock_depth);
}

static void selectRegister(uint8_t bus, uint8_t reg, bool stop) {
  jh_test_i2c[bus].received = reg;
  jh_test_i2c[bus].rx_ready = true;
  irq(bus, I2C_IC_INTR_STAT_R_START_DET_BITS | I2C_IC_INTR_STAT_R_RX_FULL_BITS);
  if (stop) {
    irq(bus, I2C_IC_INTR_STAT_R_STOP_DET_BITS);
  }
}

static uint8_t readByte(uint8_t bus, bool first) {
  irq(bus, I2C_IC_INTR_STAT_R_RD_REQ_BITS |
               (first ? I2C_IC_INTR_STAT_R_START_DET_BITS : 0U));
  return jh_test_i2c[bus].transmitted;
}

void test_publication_obeys_read_mode_at_every_byte_boundary(void) {
  uint8_t before[30], after[30];
  for (size_t i = 0; i < COUNTOF(before); ++i) {
    before[i] = (uint8_t)(i + 1U);
    after[i] = (uint8_t)(i + 101U);
  }
  for (uint8_t bus = 0; bus < 2U; ++bus) {
    for (size_t split = 0; split < COUNTOF(before); ++split) {
      TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_slave_reg_write_block_bus(
                                        bus, 1U, before, COUNTOF(before)));
      selectRegister(bus, 1U, split % 2U != 0U);
      for (size_t i = 0; i < COUNTOF(before); ++i) {
#ifdef HAL_ENABLE_I2C_SLAVE_SNAPSHOT
        const uint8_t expected = before[i];
#else
        const uint8_t expected = i > split ? after[i] : before[i];
#endif
        TEST_ASSERT_EQUAL_UINT8(expected, readByte(bus, i == 0U));
        if (i == split) {
          TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_slave_reg_write_block_bus(
                                            bus, 1U, after, COUNTOF(after)));
        }
      }
      irq(bus, I2C_IC_INTR_STAT_R_STOP_DET_BITS);
      selectRegister(bus, 1U, false);
      for (size_t i = 0; i < COUNTOF(after); ++i) {
        TEST_ASSERT_EQUAL_UINT8(after[i], readByte(bus, i == 0U));
      }
      irq(bus, I2C_IC_INTR_STAT_R_STOP_DET_BITS);
    }
  }
}

void test_short_read_stop_and_bare_read_refresh_snapshot(void) {
  hal_i2c_slave_reg_write16(0U, 0x1122U);
  selectRegister(0U, 0U, false);
  TEST_ASSERT_EQUAL_UINT8(0x11U, readByte(0U, true));
  irq(0U, I2C_IC_INTR_STAT_R_STOP_DET_BITS);
  hal_i2c_slave_reg_write8(1U, 0x33U);
  TEST_ASSERT_EQUAL_UINT8(0x33U, readByte(0U, true));
  TEST_ASSERT_EQUAL_UINT32(2U, hal_i2c_slave_get_transaction_count());
}

void test_repeated_start_and_abort_refresh_snapshot(void) {
  const uint32_t endings[] = {I2C_IC_INTR_STAT_R_START_DET_BITS,
                              I2C_IC_INTR_STAT_R_TX_ABRT_BITS};
  for (size_t i = 0; i < COUNTOF(endings); ++i) {
    hal_i2c_slave_reg_write16(0U, 0x1122U);
    selectRegister(0U, 0U, false);
    TEST_ASSERT_EQUAL_UINT8(0x11U, readByte(0U, true));
    hal_i2c_slave_reg_write8(1U, 0x44U);
    irq(0U, endings[i] | I2C_IC_INTR_STAT_R_RD_REQ_BITS);
    TEST_ASSERT_EQUAL_UINT8(0x44U, jh_test_i2c[0].transmitted);
    irq(0U, I2C_IC_INTR_STAT_R_STOP_DET_BITS);
  }
}

void test_previous_stop_coalesced_with_new_read_keeps_snapshot(void) {
  hal_i2c_slave_reg_write16(0U, 0x1122U);
  selectRegister(0U, 0U, false);
  irq(0U, I2C_IC_INTR_STAT_R_STOP_DET_BITS | I2C_IC_INTR_STAT_R_START_DET_BITS |
              I2C_IC_INTR_STAT_R_RD_REQ_BITS);
  TEST_ASSERT_EQUAL_UINT8(0x11U, jh_test_i2c[0].transmitted);
  hal_i2c_slave_reg_write16(0U, 0x3344U);
#ifdef HAL_ENABLE_I2C_SLAVE_SNAPSHOT
  TEST_ASSERT_EQUAL_UINT8(0x22U, readByte(0U, false));
#else
  TEST_ASSERT_EQUAL_UINT8(0x44U, readByte(0U, false));
#endif
}

void test_snapshot_is_taken_at_read_and_buses_are_independent(void) {
  hal_i2c_slave_reg_write8(0U, 1U);
  selectRegister(0U, 0U, false);
  hal_i2c_slave_reg_write16(0U, 0x0203U);
  TEST_ASSERT_EQUAL_UINT8(2U, readByte(0U, true));
  hal_i2c_slave_reg_write16(0U, 0x0405U);
  hal_i2c_slave_reg_write8_bus(1U, 0U, 6U);
  selectRegister(1U, 0U, false);
  TEST_ASSERT_EQUAL_UINT8(6U, readByte(1U, true));
#ifdef HAL_ENABLE_I2C_SLAVE_SNAPSHOT
  TEST_ASSERT_EQUAL_UINT8(3U, readByte(0U, false));
#else
  TEST_ASSERT_EQUAL_UINT8(5U, readByte(0U, false));
#endif
}

void test_invalid_block_is_rejected_without_partial_writes(void) {
  uint8_t values[] = {0x11, 0x22};
  const uint8_t last = HAL_I2C_SLAVE_REG_MAP_SIZE - 1U;
  hal_i2c_slave_reg_write8(last, 0x55U);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, hal_i2c_slave_reg_write_block(last, values, COUNTOF(values)));
  TEST_ASSERT_EQUAL_UINT8(0x55U, hal_i2c_slave_reg_read8(last));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_slave_reg_write_block(0, nullptr, 1));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_slave_reg_write_block(0, values, SIZE_MAX));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        hal_i2c_slave_reg_write_block_bus(2, 0, values, 1));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_slave_reg_write_block(0, nullptr, 0));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_i2c_slave_reg_write_block(last, values, 1));
  TEST_ASSERT_EQUAL_UINT8(values[0], hal_i2c_slave_reg_read8(last));
}

void test_reinit_discards_snapshot_and_read_past_map_returns_zero(void) {
  hal_i2c_slave_reg_write8(0U, 1U);
  selectRegister(0U, 0U, false);
  TEST_ASSERT_EQUAL_UINT8(1U, readByte(0U, true));
  hal_i2c_slave_init(4, 5, 0x57);
  hal_i2c_slave_reg_write8(0U, 2U);
  TEST_ASSERT_EQUAL_UINT8(2U, readByte(0U, true));
  selectRegister(0U, HAL_I2C_SLAVE_REG_MAP_SIZE - 1U, false);
  TEST_ASSERT_EQUAL_UINT8(0U, readByte(0U, true));
  TEST_ASSERT_EQUAL_UINT8(0U, readByte(0U, false));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_publication_obeys_read_mode_at_every_byte_boundary);
  RUN_TEST(test_short_read_stop_and_bare_read_refresh_snapshot);
  RUN_TEST(test_repeated_start_and_abort_refresh_snapshot);
  RUN_TEST(test_previous_stop_coalesced_with_new_read_keeps_snapshot);
  RUN_TEST(test_snapshot_is_taken_at_read_and_buses_are_independent);
  RUN_TEST(test_invalid_block_is_rejected_without_partial_writes);
  RUN_TEST(test_reinit_discards_snapshot_and_read_past_map_returns_zero);
  return UNITY_END();
}
