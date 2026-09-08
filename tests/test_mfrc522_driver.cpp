#include "hal/impl/.mock/hal_mock.h"
#include "hal/nfc/hal_mfrc522.h"
#include "utils/unity.h"

#include <cstring>

static_assert(sizeof(MFRC522_BUS_DEVICE) == sizeof(void *),
              "transport diagnostics must preserve the legacy base layout");

class InitProbeBus : public MFRC522_BUS_DEVICE {
public:
  explicit InitProbeBus(bool initResult) : initResult(initResult) {}

  bool PCD_Init() override {
    initCalls++;
    return initResult;
  }

  void PCD_WriteRegister(MFRC522::PCD_Register reg, byte value) override {
    if (reg == MFRC522::CommandReg && value == MFRC522::PCD_SoftReset) {
      softResetWrites++;
    }
  }

  byte PCD_ReadRegister(MFRC522::PCD_Register reg) override {
    if (reg == MFRC522::CommandReg) {
      return 0;
    }
    if (reg == MFRC522::TxControlReg) {
      return 0x03;
    }
    return 0;
  }

  bool initResult;
  int initCalls = 0;
  int softResetWrites = 0;
};

void setUp(void) {
  hal_mock_spi_reset();
  hal_i2c_init_bus(1, 4, 5, HAL_I2C_CLOCK_FAST_HZ);
  hal_mock_i2c_reset_write_log_bus(1);
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
}

void tearDown(void) {}

void test_mfrc522_spi_register_write_uses_hal_transaction(void) {
  hal_spi_init(1, 8, 11, 10);
  MFRC522_SPI bus(/*chipSelectPin=*/22, /*resetPowerDownPin=*/UNUSED_PIN,
                  /*bus=*/1);
  MFRC522 dev(&bus);

  dev.PCD_WriteRegister(MFRC522::CommandReg, 0x0F);

  uint8_t tx[4] = {};
  TEST_ASSERT_EQUAL_size_t(2, hal_mock_spi_get_tx(1, tx, sizeof(tx)));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(MFRC522::CommandReg << 1), tx[0]);
  TEST_ASSERT_EQUAL_UINT8(0x0F, tx[1]);
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(22));
}

void test_mfrc522_spi_register_read_preserves_rx_align_bits(void) {
  hal_spi_init(0, 0, 3, 2);
  const uint8_t rx[] = {0x00, 0xAB};
  hal_mock_spi_push_rx(0, rx, sizeof(rx));
  MFRC522_SPI bus(/*chipSelectPin=*/17);
  MFRC522 dev(&bus);

  uint8_t value = 0x05;
  dev.PCD_ReadRegister(MFRC522::FIFODataReg, 1, &value, 4);

  TEST_ASSERT_EQUAL_UINT8(0xA5, value);
}

void test_mfrc522_spi_write_failure_still_releases_device(void) {
  hal_spi_init(1, 8, 11, 10);
  MFRC522_SPI bus(/*chipSelectPin=*/22, /*resetPowerDownPin=*/UNUSED_PIN,
                  /*bus=*/1);
  MFRC522 dev(&bus);

  hal_mock_spi_fail_next_write(1, true);
  dev.PCD_WriteRegister(MFRC522::CommandReg, 0x0F);

  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(22));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1));
}

void test_mfrc522_i2c_register_read_uses_write_read_transaction(void) {
  const uint8_t rx[] = {0x92};
  hal_mock_i2c_inject_rx_bus(1, rx, sizeof(rx));
  MFRC522_I2C bus(/*resetPowerDownPin=*/UNUSED_PIN,
                  /*chipAddress=*/MFRC522_I2C_DEFAULT_ADDR, /*bus=*/1);
  MFRC522 dev(&bus);

  TEST_ASSERT_EQUAL_UINT8(0x92, dev.PCD_GetVersion());

  uint8_t frame[2] = {};
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_count_bus(1));
  TEST_ASSERT_EQUAL_INT(1, hal_mock_i2c_get_write_frame_bus(1, 0, frame, 2));
  TEST_ASSERT_EQUAL_UINT8(MFRC522::VersionReg, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(MFRC522_I2C_DEFAULT_ADDR,
                          hal_mock_i2c_get_last_addr_bus(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth_bus(1));
}

void test_mfrc522_c_facade_reports_and_clears_spi_errors(void) {
  hal_mfrc522_spi_config_t config = hal_mfrc522_spi_default_config(17u);
  hal_mfrc522_transport_t transport = nullptr;
  hal_mfrc522_t reader = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mfrc522_transport_create_spi(&config, &transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_create(transport, &reader));

  uint8_t version = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_mfrc522_get_version(reader, &version));
  uint8_t begin_rx[32];
  std::memset(begin_rx, 0x92, sizeof(begin_rx));
  hal_mock_spi_push_rx(0u, begin_rx, sizeof(begin_rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_begin(reader));

  hal_mock_spi_fail_next_write(0u, true);
  const hal_status_t failed_status = hal_mfrc522_get_version(reader, &version);

  const uint8_t version_rx[] = {0x00u, 0x92u};
  hal_mock_spi_push_rx(0u, version_rx, sizeof(version_rx));
  const hal_status_t recovered_status =
      hal_mfrc522_get_version(reader, &version);

  hal_mock_spi_fail_next_write(0u, true);
  const hal_status_t rebegin_status = hal_mfrc522_begin(reader);
  const hal_status_t state_after_failed_rebegin =
      hal_mfrc522_get_version(reader, &version);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_destroy(reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_transport_destroy(transport));
  TEST_ASSERT_EQUAL_INT(HAL_EIO, failed_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, recovered_status);
  TEST_ASSERT_EQUAL_HEX8(0x92u, version);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, rebegin_status);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, state_after_failed_rebegin);
}

void test_mfrc522_c_facade_reports_and_clears_i2c_errors(void) {
  hal_mfrc522_i2c_config_t config = hal_mfrc522_i2c_default_config();
  config.i2c_bus = 1u;
  hal_mfrc522_transport_t transport = nullptr;
  hal_mfrc522_t reader = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mfrc522_transport_create_i2c(&config, &transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_create(transport, &reader));

  uint8_t version = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_mfrc522_get_version(reader, &version));
  uint8_t begin_rx[8];
  std::memset(begin_rx, 0x91, sizeof(begin_rx));
  hal_mock_i2c_inject_rx_bus(1u, begin_rx, sizeof(begin_rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_begin(reader));

  hal_mock_i2c_set_busy_bus(1u, true);
  const hal_status_t failed_status = hal_mfrc522_get_version(reader, &version);

  hal_mock_i2c_set_busy_bus(1u, false);
  const uint8_t version_rx[] = {0x91u};
  hal_mock_i2c_inject_rx_bus(1u, version_rx, sizeof(version_rx));
  const hal_status_t recovered_status =
      hal_mfrc522_get_version(reader, &version);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_destroy(reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_mfrc522_transport_destroy(transport));
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, failed_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, recovered_status);
  TEST_ASSERT_EQUAL_HEX8(0x91u, version);
}

void test_mfrc522_legacy_transport_does_not_activate_error_latch(void) {
  hal_spi_init(0u, 4u, 7u, 6u);
  MFRC522_SPI bus(/*chipSelectPin=*/17u, /*resetPowerDownPin=*/UNUSED_PIN,
                  /*bus=*/0u);
  MFRC522 reader(&bus);

  hal_mock_spi_fail_next_write(0u, true);
  (void)reader.PCD_GetVersion();
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, bus.PCD_GetTransportError());

  TEST_ASSERT_EQUAL_INT(HAL_OK, bus.PCD_ClearTransportError());
  hal_mock_spi_fail_next_write(0u, true);
  (void)reader.PCD_GetVersion();
  TEST_ASSERT_EQUAL_INT(HAL_EIO, bus.PCD_GetTransportError());
  TEST_ASSERT_EQUAL_INT(HAL_OK, bus.PCD_ReleaseTransportError());
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, bus.PCD_GetTransportError());
}

void test_mfrc522_status_maps_to_hal_status(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        MFRC522::StatusCodeToHalStatus(MFRC522::STATUS_OK));
  TEST_ASSERT_EQUAL_INT(
      HAL_ETIMEOUT, MFRC522::StatusCodeToHalStatus(MFRC522::STATUS_TIMEOUT));
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW, MFRC522::StatusCodeToHalStatus(MFRC522::STATUS_NO_ROOM));
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, MFRC522::StatusCodeToHalStatus(MFRC522::STATUS_CRC_WRONG));
  TEST_ASSERT_EQUAL_INT(
      HAL_EAUTH, MFRC522::StatusCodeToHalStatus(MFRC522::STATUS_MIFARE_NACK));
}

void test_mfrc522_init_soft_resets_when_transport_did_not_reset(void) {
  InitProbeBus bus(false);
  MFRC522 dev(&bus);

  dev.PCD_Init();

  TEST_ASSERT_EQUAL_INT(1, bus.initCalls);
  TEST_ASSERT_EQUAL_INT(1, bus.softResetWrites);
}

void test_mfrc522_init_does_not_soft_reset_after_hardware_reset(void) {
  InitProbeBus bus(true);
  MFRC522 dev(&bus);

  dev.PCD_Init();

  TEST_ASSERT_EQUAL_INT(1, bus.initCalls);
  TEST_ASSERT_EQUAL_INT(0, bus.softResetWrites);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_mfrc522_spi_register_write_uses_hal_transaction);
  RUN_TEST(test_mfrc522_spi_register_read_preserves_rx_align_bits);
  RUN_TEST(test_mfrc522_spi_write_failure_still_releases_device);
  RUN_TEST(test_mfrc522_i2c_register_read_uses_write_read_transaction);
  RUN_TEST(test_mfrc522_c_facade_reports_and_clears_spi_errors);
  RUN_TEST(test_mfrc522_c_facade_reports_and_clears_i2c_errors);
  RUN_TEST(test_mfrc522_legacy_transport_does_not_activate_error_latch);
  RUN_TEST(test_mfrc522_status_maps_to_hal_status);
  RUN_TEST(test_mfrc522_init_soft_resets_when_transport_did_not_reset);
  RUN_TEST(test_mfrc522_init_does_not_soft_reset_after_hardware_reset);
  return UNITY_END();
}
