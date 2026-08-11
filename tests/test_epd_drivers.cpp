#include "utils/unity.h"

#include "hal/display/drivers/epd_spi_transport.h"
#include "hal/display/drivers/ssd16xx_driver.h"
#include "hal/display/drivers/uc81xx_driver.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_set_millis(0u);
  hal_mock_gpio_inject_level(12u, false);
}

void tearDown(void) {}

static bool tx_contains(const uint8_t *needle, size_t needle_len) {
  uint8_t tx[512] = {};
  const size_t tx_len = hal_mock_spi_get_tx(0u, tx, sizeof(tx));
  if (needle == NULL || needle_len == 0u || tx_len < needle_len) {
    return false;
  }
  for (size_t i = 0u; i <= tx_len - needle_len; ++i) {
    if (memcmp(tx + i, needle, needle_len) == 0) {
      return true;
    }
  }
  return false;
}

static jh_epd_spi_config_t transport_config(void) {
  jh_epd_spi_config_t config = {};
  config.bus = 0u;
  config.cs_pin = 10;
  config.dc_pin = 11;
  config.rst_pin = -1;
  config.busy_pin = 12;
  config.busy_timeout_ms = 4u;
  config.busy_active_high = true;
  return config;
}

void test_epd_transport_times_out_when_busy_does_not_clear(void) {
  jh_epd_spi_t transport = {};
  const jh_epd_spi_config_t config = transport_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_epd_spi_init(&transport, &config));
  hal_mock_gpio_inject_level(12u, true);
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        jh_epd_spi_command(&transport, 0x12u, NULL, 0u));
  TEST_ASSERT_EQUAL_UINT32(4u, hal_millis());
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
}

void test_epd_transport_write_failure_releases_device(void) {
  jh_epd_spi_t transport = {};
  const jh_epd_spi_config_t config = transport_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_epd_spi_init(&transport, &config));

  hal_mock_spi_fail_next_write(config.bus, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO,
                        jh_epd_spi_command(&transport, 0x12u, NULL, 0u));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state((uint8_t)config.cs_pin));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(config.bus));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(config.bus));
}

void test_ssd16xx_initializes_writes_window_and_refreshes(void) {
  jh_ssd16xx_t dev = {};
  jh_ssd16xx_config_t config = {};
  config.controller = JH_SSD16XX_SSD1680;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_init(&dev, &config));
  TEST_ASSERT_TRUE(dev.initialized);

  hal_mock_spi_reset();
  const uint8_t pixels[] = {0xAAu, 0x55u, 0xAAu, 0x55u};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_write(&dev, 2u, 8u, 4u, 8u, pixels,
                                                 sizeof(pixels), false));
  const uint8_t area_command[] = {0x44u};
  const uint8_t ram_write[] = {0x24u, 0xAAu, 0x55u, 0xAAu, 0x55u};
  TEST_ASSERT_TRUE(tx_contains(area_command, sizeof(area_command)));
  TEST_ASSERT_TRUE(tx_contains(ram_write, sizeof(ram_write)));
  TEST_ASSERT_TRUE(dev.refresh_pending);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_ssd16xx_refresh(&dev, JH_SSD16XX_REFRESH_PARTIAL));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ssd16xx_refresh(&dev, JH_SSD16XX_REFRESH_FULL));
  const uint8_t activation[] = {0x20u};
  TEST_ASSERT_TRUE(tx_contains(activation, sizeof(activation)));
  TEST_ASSERT_FALSE(dev.refresh_pending);
}

void test_ssd16xx_rejects_non_page_aligned_normal_write(void) {
  jh_ssd16xx_t dev = {};
  jh_ssd16xx_config_t config = {};
  config.controller = JH_SSD16XX_SSD1680;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;
  const uint8_t pixels[4] = {};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_init(&dev, &config));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_ssd16xx_write(&dev, 0u, 1u, 4u, 8u, pixels, sizeof(pixels), false));
}

void test_ssd16xx_partial_profile_refreshes_immediately_and_protects_full_batch(
    void) {
  static const uint8_t lut[] = {0x01u, 0x02u};
  static const jh_ssd16xx_profile_t partial_profile = {
      .lut = {lut, sizeof(lut)},
  };
  jh_ssd16xx_t dev = {};
  jh_ssd16xx_config_t config = {};
  config.controller = JH_SSD16XX_SSD1680;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;
  config.partial_profile = &partial_profile;
  const uint8_t pixels[4] = {0xA5u, 0x5Au, 0xA5u, 0x5Au};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_init(&dev, &config));
  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_write(&dev, 0u, 0u, 4u, 8u, pixels,
                                                 sizeof(pixels), true));
  const uint8_t lut_command[] = {0x32u, 0x01u, 0x02u};
  TEST_ASSERT_TRUE(tx_contains(lut_command, sizeof(lut_command)));
  TEST_ASSERT_EQUAL_INT(JH_SSD16XX_REFRESH_PARTIAL, dev.profile);
  TEST_ASSERT_FALSE(dev.refresh_pending);

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_write(&dev, 0u, 0u, 4u, 8u, pixels,
                                                 sizeof(pixels), false));
  TEST_ASSERT_EQUAL_INT(JH_SSD16XX_REFRESH_FULL, dev.pending_refresh_mode);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        jh_ssd16xx_refresh(&dev, JH_SSD16XX_REFRESH_PARTIAL));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ssd16xx_refresh(&dev, JH_SSD16XX_REFRESH_FULL));
}

void test_uc81xx_initializes_writes_partial_window_and_refreshes(void) {
  jh_uc81xx_t dev = {};
  jh_uc81xx_config_t config = {};
  config.controller = JH_UC81XX_UC8175;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_init(&dev, &config));
  TEST_ASSERT_TRUE(dev.initialized);

  hal_mock_spi_reset();
  const uint8_t pixels[] = {0xF0u, 0x0Fu, 0xF0u, 0x0Fu,
                            0xF0u, 0x0Fu, 0xF0u, 0x0Fu};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_write(&dev, 0u, 4u, 8u, 8u, pixels,
                                                sizeof(pixels), false));
  const uint8_t partial_window[] = {0x90u, 0x00u, 0x07u, 0x04u, 0x0Bu, 0x01u};
  TEST_ASSERT_TRUE(tx_contains(partial_window, sizeof(partial_window)));
  const uint8_t ram_write[] = {0x13u, 0xF0u, 0x0Fu};
  TEST_ASSERT_TRUE(tx_contains(ram_write, sizeof(ram_write)));
  TEST_ASSERT_TRUE(dev.refresh_pending);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        jh_uc81xx_refresh(&dev, JH_UC81XX_REFRESH_PARTIAL));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_uc81xx_refresh(&dev, JH_UC81XX_REFRESH_FULL));
  const uint8_t refresh_sequence[] = {0x04u, 0x12u, 0x02u};
  TEST_ASSERT_TRUE(tx_contains(refresh_sequence, sizeof(refresh_sequence)));
  TEST_ASSERT_FALSE(dev.refresh_pending);
}

void test_uc81xx_rejects_unaligned_horizontal_write(void) {
  jh_uc81xx_t dev = {};
  jh_uc81xx_config_t config = {};
  config.controller = JH_UC81XX_UC8175;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;
  const uint8_t pixels[8] = {};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_init(&dev, &config));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_uc81xx_write(&dev, 1u, 0u, 8u, 8u, pixels, sizeof(pixels), false));
}

void test_uc81xx_partial_profile_refreshes_after_full_batch_is_committed(void) {
  static const jh_uc81xx_profile_t partial_profile = {
      .pll = 0x3Au,
      .override_pll = true,
  };
  jh_uc81xx_t dev = {};
  jh_uc81xx_config_t config = {};
  config.controller = JH_UC81XX_UC8175;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;
  config.partial_profile = &partial_profile;
  const uint8_t pixels[8] = {0x3Cu, 0xC3u, 0x3Cu, 0xC3u,
                             0x3Cu, 0xC3u, 0x3Cu, 0xC3u};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_init(&dev, &config));
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        jh_uc81xx_refresh(&dev, JH_UC81XX_REFRESH_PARTIAL));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_uc81xx_refresh(&dev, JH_UC81XX_REFRESH_FULL));

  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_write(&dev, 0u, 0u, 8u, 8u, pixels,
                                                sizeof(pixels), true));
  const uint8_t pll_command[] = {0x30u, 0x3Au};
  TEST_ASSERT_TRUE(tx_contains(pll_command, sizeof(pll_command)));
  TEST_ASSERT_EQUAL_INT(JH_UC81XX_REFRESH_PARTIAL, dev.profile);
  TEST_ASSERT_FALSE(dev.refresh_pending);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_epd_transport_times_out_when_busy_does_not_clear);
  RUN_TEST(test_epd_transport_write_failure_releases_device);
  RUN_TEST(test_ssd16xx_initializes_writes_window_and_refreshes);
  RUN_TEST(test_ssd16xx_rejects_non_page_aligned_normal_write);
  RUN_TEST(
      test_ssd16xx_partial_profile_refreshes_immediately_and_protects_full_batch);
  RUN_TEST(test_uc81xx_initializes_writes_partial_window_and_refreshes);
  RUN_TEST(test_uc81xx_rejects_unaligned_horizontal_write);
  RUN_TEST(test_uc81xx_partial_profile_refreshes_after_full_batch_is_committed);
  return UNITY_END();
}
