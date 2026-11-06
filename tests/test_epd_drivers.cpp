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

struct StridedTransfer {
  unsigned selects;
  unsigned deselects;
  unsigned bytes;
  bool fail_second_row;
};

static void strided_select(void *user) { ++((StridedTransfer *)user)->selects; }

static void strided_deselect(void *user) {
  ++((StridedTransfer *)user)->deselects;
}

static uint8_t strided_exchange(void *user, uint8_t value) {
  (void)value;
  StridedTransfer *transfer = (StridedTransfer *)user;
  ++transfer->bytes;
  if (transfer->fail_second_row && transfer->bytes == 3u) {
    hal_mock_spi_fail_next_write(0u, true);
  }
  return 0xFFu;
}

void test_epd_strided_transport_keeps_one_transaction_and_releases_on_failure(
    void) {
  jh_epd_spi_t transport = {};
  const jh_epd_spi_config_t config = transport_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_epd_spi_init(&transport, &config));
  const uint8_t pixels[] = {0xA1u, 0xB2u, 0xEEu, 0xEEu, 0xC3u, 0xD4u};
  const uint8_t expected[] = {0x13u, 0xA1u, 0xB2u, 0xC3u, 0xD4u};
  const hal_mock_spi_device_t peripheral = {strided_select, strided_exchange,
                                            strided_deselect};
  for (unsigned failure = 0u; failure < 3u; ++failure) {
    hal_mock_spi_reset();
    StridedTransfer transfer = {0u, 0u, 0u, failure == 1u};
    hal_mock_spi_attach_device(0u, 10u, &peripheral, &transfer);
    hal_mock_spi_fail_next_end(0u, failure == 2u);
    TEST_ASSERT_EQUAL_INT(
        failure == 0u ? HAL_OK : HAL_EIO,
        jh_epd_spi_command_strided(&transport, 0x13u, pixels, 2u, 4u, 2u));
    uint8_t tx[16] = {};
    const size_t count = failure == 1u ? 3u : sizeof(expected);
    TEST_ASSERT_EQUAL_size_t(count, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, tx, count);
    TEST_ASSERT_EQUAL_UINT(1u, transfer.selects);
    TEST_ASSERT_EQUAL_UINT(1u, transfer.deselects);
    TEST_ASSERT_TRUE(hal_mock_gpio_get_state(10u));
    TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
    TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
    hal_mock_spi_reset(); // Detach the peripheral before its stack state
                          // expires.
  }
}

void test_epd_strided_transport_rejects_invalid_sizes_before_spi(void) {
  jh_epd_spi_t transport = {};
  const jh_epd_spi_config_t config = transport_config();
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_epd_spi_init(&transport, &config));
  const uint8_t pixels[] = {0xA1u, 0xB2u};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_epd_spi_command_strided(
                                        &transport, 0x13u, pixels, 2u, 1u, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_epd_spi_command_strided(
                                        &transport, 0x13u, pixels, 2u, 2u, 0u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_epd_spi_command_strided(
                                        &transport, 0x13u, NULL, 2u, 2u, 1u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_epd_spi_command_strided(&transport, 0x13u, pixels, 2u, SIZE_MAX, 2u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, jh_epd_spi_command_strided(
                                        &transport, 0x13u, NULL, 0u, 0u, 2u));
  uint8_t tx[4] = {};
  TEST_ASSERT_EQUAL_size_t(0u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
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

void test_ssd16xx_rejects_pitch_below_width_before_spi(void) {
  jh_ssd16xx_config_t config = {};
  config.controller = JH_SSD16XX_SSD1680;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;
  const uint8_t pixels[16] = {};
  for (uint8_t rotation = 0u; rotation < 4u; ++rotation) {
    for (unsigned pending = 0u; pending < 2u; ++pending) {
      jh_ssd16xx_t dev = {};
      config.rotation = rotation;
      TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_init(&dev, &config));
      if (pending != 0u) {
        TEST_ASSERT_EQUAL_INT(HAL_OK,
                              jh_ssd16xx_write(&dev, 0u, 0u, 16u, 8u, pixels,
                                               sizeof(pixels), false));
      }
      hal_mock_spi_reset();
      const jh_ssd16xx_refresh_mode_t profile = dev.profile;
      const jh_ssd16xx_refresh_mode_t mode = dev.pending_refresh_mode;
      // Both values are aligned; rejection must come from pitch < width.
      TEST_ASSERT_EQUAL_INT(
          HAL_EINVAL, jh_ssd16xx_write_pitched(&dev, 0u, 0u, 16u, 8u, 8u,
                                               pixels, sizeof(pixels), true));
      uint8_t tx[1] = {};
      TEST_ASSERT_EQUAL_size_t(0u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
      TEST_ASSERT_EQUAL_INT(pending != 0u, dev.refresh_pending);
      TEST_ASSERT_EQUAL_INT(mode, dev.pending_refresh_mode);
      TEST_ASSERT_EQUAL_INT(profile, dev.profile);
      TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
      TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
    }
  }
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

void test_uc81xx_rejects_pitch_below_width_before_spi(void) {
  static const jh_uc81xx_controller_t controllers[] = {
      JH_UC81XX_UC8175, JH_UC81XX_UC8176, JH_UC81XX_UC8179, JH_UC81XX_UC8151D};
  jh_uc81xx_config_t config = {};
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;
  const uint8_t pixels[4] = {};
  for (size_t i = 0u; i < COUNTOF(controllers); ++i) {
    for (unsigned pending = 0u; pending < 2u; ++pending) {
      jh_uc81xx_t dev = {};
      config.controller = controllers[i];
      TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_init(&dev, &config));
      if (pending == 0u) {
        TEST_ASSERT_EQUAL_INT(HAL_OK,
                              jh_uc81xx_refresh(&dev, JH_UC81XX_REFRESH_FULL));
      }
      hal_mock_spi_reset();
      const jh_uc81xx_refresh_mode_t profile = dev.profile;
      const jh_uc81xx_refresh_mode_t mode = dev.pending_refresh_mode;
      TEST_ASSERT_EQUAL_INT(
          HAL_EINVAL, jh_uc81xx_write_pitched(&dev, 0u, 0u, 16u, 2u, 8u, pixels,
                                              sizeof(pixels), true));
      uint8_t tx[1] = {};
      TEST_ASSERT_EQUAL_size_t(0u, hal_mock_spi_get_tx(0u, tx, sizeof(tx)));
      TEST_ASSERT_EQUAL_INT(pending != 0u, dev.refresh_pending);
      TEST_ASSERT_EQUAL_INT(mode, dev.pending_refresh_mode);
      TEST_ASSERT_EQUAL_INT(profile, dev.profile);
      TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(0u));
      TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(0u));
    }
  }
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

struct TxCapture {
  uint8_t bytes[512];
  size_t len;
};

static void capture_tx(TxCapture *capture) {
  capture->len =
      hal_mock_spi_get_tx(0u, capture->bytes, sizeof(capture->bytes));
  // The mock log holds 512 bytes; a full log may have dropped some.
  TEST_ASSERT_LESS_THAN_size_t(sizeof(capture->bytes), capture->len);
}

static void assert_same_tx(const TxCapture *expected, const TxCapture *actual) {
  TEST_ASSERT_EQUAL_size_t(expected->len, actual->len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->bytes, actual->bytes, expected->len);
}

static const uint8_t kAreaA[8] = {0x11u, 0x22u, 0x33u, 0x44u,
                                  0x55u, 0x66u, 0x77u, 0x88u};
static const uint8_t kAreaB[8] = {0xA5u, 0x5Au, 0xC3u, 0x3Cu,
                                  0xA5u, 0x5Au, 0xC3u, 0x3Cu};

static void ssd16xx_with_pending_batch(jh_ssd16xx_t *dev,
                                       const jh_ssd16xx_config_t *config) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_ssd16xx_init(dev, config));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ssd16xx_write(dev, 0u, 0u, 4u, 8u, kAreaA, 4u, false));
  hal_mock_spi_reset();
}

void test_ssd16xx_refreshing_write_commits_pending_full_batch(void) {
  static const uint8_t lut[] = {0x01u, 0x02u};
  static jh_ssd16xx_profile_t partial_profile = {};
  partial_profile.lut.data = lut;
  partial_profile.lut.len = sizeof(lut);
  jh_ssd16xx_config_t config = {};
  config.controller = JH_SSD16XX_SSD1680;
  config.transport = transport_config();
  config.width = 16u;
  config.height = 16u;
  config.partial_profile = &partial_profile;

  // Reference: area B joins the deferred batch, then the flush's full
  // refresh commits it.
  jh_ssd16xx_t reference = {};
  ssd16xx_with_pending_batch(&reference, &config);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ssd16xx_write(&reference, 8u, 8u, 4u, 8u, kAreaB, 4u, false));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ssd16xx_refresh(&reference, JH_SSD16XX_REFRESH_FULL));
  TxCapture expected = {};
  capture_tx(&expected);

  // A refreshing write must not turn the pending full refresh into a partial
  // one, which would leave area A stale on the panel.
  jh_ssd16xx_t dev = {};
  ssd16xx_with_pending_batch(&dev, &config);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ssd16xx_write(&dev, 8u, 8u, 4u, 8u, kAreaB, 4u, true));
  TxCapture actual = {};
  capture_tx(&actual);
  assert_same_tx(&expected, &actual);
  const uint8_t old_image[] = {0x26u, 0xA5u, 0x5Au, 0xC3u, 0x3Cu};
  TEST_ASSERT_TRUE(tx_contains(old_image, sizeof(old_image)));
  TEST_ASSERT_EQUAL_INT(JH_SSD16XX_REFRESH_FULL, dev.profile);
  TEST_ASSERT_FALSE(dev.refresh_pending);

  // With nothing pending, a refreshing write is partial again.
  hal_mock_spi_reset();
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_ssd16xx_write(&dev, 8u, 8u, 4u, 8u, kAreaB, 4u, true));
  TEST_ASSERT_EQUAL_INT(JH_SSD16XX_REFRESH_PARTIAL, dev.profile);
  TEST_ASSERT_FALSE(dev.refresh_pending);
}

static void uc81xx_with_pending_full(jh_uc81xx_t *dev,
                                     const jh_uc81xx_config_t *config,
                                     bool deferred_area) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_init(dev, config));
  if (deferred_area) {
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          jh_uc81xx_refresh(dev, JH_UC81XX_REFRESH_FULL));
    TEST_ASSERT_EQUAL_INT(HAL_OK, jh_uc81xx_write(dev, 0u, 0u, 8u, 8u, kAreaA,
                                                  sizeof(kAreaA), false));
  }
  hal_mock_spi_reset();
}

void test_uc81xx_refreshing_write_commits_pending_full_refresh(void) {
  static jh_uc81xx_profile_t partial_profile = {};
  partial_profile.pll = 0x3Au;
  partial_profile.override_pll = true;
  const uint8_t ptout = 0x92u;
  const uint8_t whole_panel_refresh[] = {ptout, 0x04u, 0x12u, 0x02u};
  for (unsigned controller = 0u; controller <= JH_UC81XX_UC8179; ++controller) {
    for (unsigned profile = 0u; profile < 2u; ++profile) {
      // Pending after initialization, or after a deferred write.
      for (unsigned deferred = 0u; deferred < 2u; ++deferred) {
        jh_uc81xx_config_t config = {};
        config.controller = (jh_uc81xx_controller_t)controller;
        config.transport = transport_config();
        config.width = 16u;
        config.height = 16u;
        config.partial_profile = profile == 0u ? NULL : &partial_profile;

        jh_uc81xx_t reference = {};
        uc81xx_with_pending_full(&reference, &config, deferred != 0u);
        TEST_ASSERT_EQUAL_INT(HAL_OK,
                              jh_uc81xx_write(&reference, 8u, 8u, 8u, 8u,
                                              kAreaB, sizeof(kAreaB), false));
        TEST_ASSERT_EQUAL_INT(
            HAL_OK, jh_uc81xx_refresh(&reference, JH_UC81XX_REFRESH_FULL));
        TxCapture expected = {};
        capture_tx(&expected);

        // A refresh inside PTIN would cover only B's window; the pending
        // full refresh has to reach the whole panel after PTOUT.
        jh_uc81xx_t dev = {};
        uc81xx_with_pending_full(&dev, &config, deferred != 0u);
        TEST_ASSERT_EQUAL_INT(HAL_OK,
                              jh_uc81xx_write(&dev, 8u, 8u, 8u, 8u, kAreaB,
                                              sizeof(kAreaB), true));
        TxCapture actual = {};
        capture_tx(&actual);
        assert_same_tx(&expected, &actual);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(whole_panel_refresh,
                                      actual.bytes + actual.len -
                                          sizeof(whole_panel_refresh),
                                      sizeof(whole_panel_refresh));
        TEST_ASSERT_EQUAL_INT(JH_UC81XX_REFRESH_FULL, dev.profile);
        TEST_ASSERT_FALSE(dev.refresh_pending);

        // With nothing pending, the refresh covers the window again.
        hal_mock_spi_reset();
        TEST_ASSERT_EQUAL_INT(HAL_OK,
                              jh_uc81xx_write(&dev, 8u, 8u, 8u, 8u, kAreaB,
                                              sizeof(kAreaB), true));
        capture_tx(&actual);
        TEST_ASSERT_EQUAL_HEX8(ptout, actual.bytes[actual.len - 1u]);
        TEST_ASSERT_EQUAL_INT(profile == 0u ? JH_UC81XX_REFRESH_FULL
                                            : JH_UC81XX_REFRESH_PARTIAL,
                              dev.profile);
        TEST_ASSERT_FALSE(dev.refresh_pending);
      }
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_epd_transport_times_out_when_busy_does_not_clear);
  RUN_TEST(test_epd_transport_write_failure_releases_device);
  RUN_TEST(
      test_epd_strided_transport_keeps_one_transaction_and_releases_on_failure);
  RUN_TEST(test_epd_strided_transport_rejects_invalid_sizes_before_spi);
  RUN_TEST(test_ssd16xx_initializes_writes_window_and_refreshes);
  RUN_TEST(test_ssd16xx_rejects_non_page_aligned_normal_write);
  RUN_TEST(test_ssd16xx_rejects_pitch_below_width_before_spi);
  RUN_TEST(
      test_ssd16xx_partial_profile_refreshes_immediately_and_protects_full_batch);
  RUN_TEST(test_uc81xx_initializes_writes_partial_window_and_refreshes);
  RUN_TEST(test_uc81xx_rejects_unaligned_horizontal_write);
  RUN_TEST(test_uc81xx_rejects_pitch_below_width_before_spi);
  RUN_TEST(test_uc81xx_partial_profile_refreshes_after_full_batch_is_committed);
  RUN_TEST(test_ssd16xx_refreshing_write_commits_pending_full_batch);
  RUN_TEST(test_uc81xx_refreshing_write_commits_pending_full_refresh);
  return UNITY_END();
}
