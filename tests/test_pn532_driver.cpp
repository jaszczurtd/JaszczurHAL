#include "hal/hal_pn532.h"
#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <cstring>

class FakePN532Bus : public PN532_BUS_DEVICE {
public:
  hal_status_t isReady(bool *ready) override {
    if (ready == nullptr) {
      return HAL_EINVAL;
    }
    *ready = readyState;
    readyCalls++;
    return readyStatus;
  }

  hal_status_t writeCommand(const uint8_t *data, size_t len) override {
    if (data == nullptr || len > sizeof(lastCommand)) {
      return HAL_EINVAL;
    }
    std::memcpy(lastCommand, data, len);
    lastCommandLen = len;
    return writeStatus;
  }

  hal_status_t readData(uint8_t *data, size_t len) override {
    if (data == nullptr || len > sizeof(readQueue)) {
      return HAL_EINVAL;
    }
    if (readOffset + len > readLen) {
      return HAL_EPROTO;
    }
    std::memcpy(data, readQueue + readOffset, len);
    readOffset += len;
    return HAL_OK;
  }

  void queue(const uint8_t *data, size_t len) {
    std::memcpy(readQueue, data, len);
    readLen = len;
    readOffset = 0;
  }

  uint8_t lastCommand[80] = {};
  size_t lastCommandLen = 0;
  uint8_t readQueue[160] = {};
  size_t readLen = 0;
  size_t readOffset = 0;
  int readyCalls = 0;
  bool readyState = true;
  hal_status_t readyStatus = HAL_OK;
  hal_status_t writeStatus = HAL_OK;
};

static void make_response(uint8_t command, const uint8_t *payload,
                          size_t payload_len, uint8_t *out, size_t *out_len) {
  out[0] = PN532_PREAMBLE;
  out[1] = PN532_STARTCODE1;
  out[2] = PN532_STARTCODE2;
  out[3] = (uint8_t)(payload_len + 2);
  out[4] = (uint8_t)(~out[3] + 1);
  out[5] = PN532_PN532TOHOST;
  out[6] = (uint8_t)(command + 1);
  std::memcpy(out + 7, payload, payload_len);

  uint8_t checksum = PN532_PN532TOHOST;
  checksum = (uint8_t)(checksum + out[6]);
  for (size_t i = 0; i < payload_len; ++i) {
    checksum = (uint8_t)(checksum + payload[i]);
  }
  out[7 + payload_len] = (uint8_t)(~checksum + 1);
  out[8 + payload_len] = PN532_POSTAMBLE;
  *out_len = payload_len + 9;
}

void setUp(void) {
  hal_mock_spi_reset();
  hal_mock_gpio_trace_reset();
  hal_mock_mutex_stats_reset();
}

void tearDown(void) {}

void test_pn532_command_frame_matches_reference_layout(void) {
  static const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  FakePN532Bus bus;
  bus.queue(ack, sizeof(ack));
  PN532 nfc(&bus);

  const uint8_t command[] = {PN532_COMMAND_GETFIRMWAREVERSION};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        nfc.sendCommandCheckAck(command, sizeof(command)));

  const uint8_t expected[] = {0x00, 0x00, 0xFF, 0x02, 0xFE,
                              0xD4, 0x02, 0x2A, 0x00};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), bus.lastCommandLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bus.lastCommand, sizeof(expected));
}

void test_pn532_get_firmware_version_parses_response(void) {
  static const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  uint8_t response[20] = {};
  size_t response_len = 0;
  const uint8_t payload[] = {0x32, 0x01, 0x06, 0x07};
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                response, &response_len);

  uint8_t queue[sizeof(ack) + sizeof(response)] = {};
  std::memcpy(queue, ack, sizeof(ack));
  std::memcpy(queue + sizeof(ack), response, response_len);

  FakePN532Bus bus;
  bus.queue(queue, sizeof(ack) + response_len);
  PN532 nfc(&bus);

  uint32_t version = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK, nfc.getFirmwareVersion(&version));
  TEST_ASSERT_EQUAL_UINT32(0x32010607u, version);
}

void test_pn532_rejects_bad_ack(void) {
  static const uint8_t bad_ack[] = {0x00, 0x00, 0xFF, 0x01, 0xFE, 0x00};
  FakePN532Bus bus;
  bus.queue(bad_ack, sizeof(bad_ack));
  PN532 nfc(&bus);

  const uint8_t command[] = {PN532_COMMAND_GETFIRMWAREVERSION};
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        nfc.sendCommandCheckAck(command, sizeof(command)));
}

void test_pn532_spi_status_read_uses_hal_transaction(void) {
  hal_spi_init(1, 8, 11, 10);
  const uint8_t rx[] = {0x00, 0x01};
  hal_mock_spi_push_rx(1, rx, sizeof(rx));
  PN532_SPI bus(/*chipSelectPin=*/22, /*resetPin=*/PN532_UNUSED_PIN,
                /*bus=*/1);

  bool ready = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, bus.isReady(&ready));

  uint8_t tx[4] = {};
  TEST_ASSERT_EQUAL_size_t(2, hal_mock_spi_get_tx(1, tx, sizeof(tx)));
  TEST_ASSERT_EQUAL_UINT8(0x02, tx[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, tx[1]);
  TEST_ASSERT_TRUE(ready);
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1));
}

void test_pn532_check_response_frame_detects_checksum_error(void) {
  uint8_t frame[20] = {};
  size_t frame_len = 0;
  const uint8_t payload[] = {0x32, 0x01, 0x06, 0x07};
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                frame, &frame_len);
  frame[frame_len - 2] ^= 0x01;

  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, PN532::checkResponseFrame(frame, frame_len,
                                            PN532_COMMAND_GETFIRMWAREVERSION));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pn532_command_frame_matches_reference_layout);
  RUN_TEST(test_pn532_get_firmware_version_parses_response);
  RUN_TEST(test_pn532_rejects_bad_ack);
  RUN_TEST(test_pn532_spi_status_read_uses_hal_transaction);
  RUN_TEST(test_pn532_check_response_frame_detects_checksum_error);
  return UNITY_END();
}
