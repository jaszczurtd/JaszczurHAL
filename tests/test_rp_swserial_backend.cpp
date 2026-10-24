// The RP software UART backend on simulated PIO and DMA, built once per chip:
// received frames must reach the reader, and keep doing so after the DMA run
// that moves them has to be restarted. RP2350 keeps a MODE field in the top
// bits of the DMA transfer count, where an all-ones count means ENDLESS: a run
// that never counts down hides every received word from the reader.

#include "hal/serial/hal_swserial.h"
#include "rp_pio_fake.h"
#include "utils/unity.h"

#include <string.h>

#define RX_PIN 22u
#define TX_PIN 21u

static hal_swserial_t s_serial;

void setUp(void) {
  jh_fake_pio_reset();
  s_serial = NULL;
  TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_create_ex(RX_PIN, TX_PIN, &s_serial));
  TEST_ASSERT_EQUAL(HAL_OK,
                    hal_swserial_begin(s_serial, 9600u, HAL_UART_CFG_8N1));
}

void tearDown(void) { hal_swserial_destroy(s_serial); }

static void send(const char *text) {
  for (size_t i = 0u; text[i] != '\0'; ++i) {
    TEST_ASSERT_TRUE(jh_fake_uart_receive(RX_PIN, (uint8_t)text[i]));
  }
}

static void expect(const char *text) {
  const size_t length = strlen(text);
  TEST_ASSERT_EQUAL_INT((int)length, hal_swserial_available(s_serial));
  for (size_t i = 0u; i < length; ++i) {
    uint8_t value = 0u;
    TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_read_ex(s_serial, &value));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)text[i], value);
  }
  uint8_t none = 0u;
  TEST_ASSERT_EQUAL(HAL_EAGAIN, hal_swserial_read_ex(s_serial, &none));
}

static void test_received_frames_reach_the_reader(void) {
  send("$GPRMC,1");
  expect("$GPRMC,1");
  send("23519,A*6C");
  expect("23519,A*6C");
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_uart_dropped());
}

static void test_reception_continues_across_a_dma_restart(void) {
  const uint32_t run = jh_fake_dma_rx_remaining(RX_PIN);
  TEST_ASSERT_GREATER_THAN_UINT32(1u, run);

  // A burst nobody read leaves one transfer in the run; the next poll must
  // start a new run before this one ends.
  TEST_ASSERT_EQUAL_UINT32(run - 1u,
                           jh_fake_dma_rx_elapse(RX_PIN, run - 1u, 'x'));
  int pending = hal_swserial_available(s_serial);
  TEST_ASSERT_GREATER_THAN_INT(0, pending);
  while (pending-- > 0) {
    uint8_t value = 0u;
    TEST_ASSERT_EQUAL(HAL_OK, hal_swserial_read_ex(s_serial, &value));
    TEST_ASSERT_EQUAL_HEX8('x', value);
  }
  TEST_ASSERT_GREATER_THAN_UINT32(1u, jh_fake_dma_rx_remaining(RX_PIN));

  send("OK");
  expect("OK");
  TEST_ASSERT_EQUAL_UINT32(0u, jh_fake_uart_dropped());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_received_frames_reach_the_reader);
  RUN_TEST(test_reception_continues_across_a_dma_restart);
  return UNITY_END();
}
