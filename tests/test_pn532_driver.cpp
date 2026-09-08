#include "hal/impl/.mock/hal_mock.h"
#include "hal/nfc/hal_pn532.h"
#include "utils/unity.h"

#include <cstring>
#include <new>

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
    readCalls++;
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
  int readCalls = 0;
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

static const uint8_t PN532_ACK[] = {0x00u, 0x00u, 0xFFu, 0x00u, 0xFFu, 0x00u};

static hal_status_t read_transport_frame(PN532_BUS_DEVICE *bus, uint8_t command,
                                         uint8_t *frame,
                                         size_t frame_capacity) {
  if (bus == nullptr || frame == nullptr || frame_capacity < 5u) {
    return HAL_EINVAL;
  }

  uint8_t header[5] = {};
  hal_status_t status = bus->readData(header, sizeof(header));
  if (status != HAL_OK) {
    return status;
  }
  std::memcpy(frame, header, sizeof(header));
  const size_t frame_size = (size_t)header[3] + 7u;
  size_t remaining = frame_size - sizeof(header);
  size_t stored = sizeof(header);
  if (stored < frame_capacity) {
    const size_t writable = frame_capacity - stored;
    const size_t read_size = remaining < writable ? remaining : writable;
    status = bus->readData(frame + stored, read_size);
    if (status != HAL_OK) {
      return status;
    }
    stored += read_size;
    remaining -= read_size;
  }

  uint8_t discard[32] = {};
  while (remaining > 0u) {
    const size_t read_size =
        remaining < sizeof(discard) ? remaining : sizeof(discard);
    status = bus->readData(discard, read_size);
    if (status != HAL_OK) {
      return status;
    }
    remaining -= read_size;
  }
  return frame_size > frame_capacity
             ? HAL_EOVERFLOW
             : PN532::checkResponseFrame(frame, frame_size, command);
}

static void make_oversized_response(uint8_t command, uint8_t *frame,
                                    size_t *frame_size) {
  uint8_t payload[253] = {};
  make_response(command, payload, sizeof(payload), frame, frame_size);
}

static void assert_passive_target_uid(const uint8_t *payload,
                                      size_t payload_size,
                                      const uint8_t *expected_uid,
                                      size_t expected_uid_size) {
  uint8_t response[32] = {};
  size_t response_size = 0u;
  make_response(PN532_COMMAND_INLISTPASSIVETARGET, payload, payload_size,
                response, &response_size);
  uint8_t queue[sizeof(PN532_ACK) + sizeof(response)] = {};
  std::memcpy(queue, PN532_ACK, sizeof(PN532_ACK));
  std::memcpy(queue + sizeof(PN532_ACK), response, response_size);

  FakePN532Bus bus;
  bus.queue(queue, sizeof(PN532_ACK) + response_size);
  PN532 nfc(&bus);
  uint8_t uid[HAL_PN532_UID_MAX_SIZE] = {};
  uint8_t uid_size = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uid_size));
  TEST_ASSERT_EQUAL_size_t(expected_uid_size, uid_size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_uid, uid, expected_uid_size);
}

struct FirmwareRecoveryQueue {
  uint8_t bytes[2u * (sizeof(PN532_ACK) + 20u)] = {};
  size_t size = 0u;
};

static FirmwareRecoveryQueue make_bad_lcs_recovery_queue(void) {
  static const uint8_t payload[] = {0x32u, 0x01u, 0x06u, 0x07u};
  FirmwareRecoveryQueue queue;

  std::memcpy(queue.bytes + queue.size, PN532_ACK, sizeof(PN532_ACK));
  queue.size += sizeof(PN532_ACK);
  size_t frame_size = 0u;
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                queue.bytes + queue.size, &frame_size);
  queue.bytes[queue.size + 4u] ^= 0x01u;
  queue.size += frame_size;
  std::memcpy(queue.bytes + queue.size, PN532_ACK, sizeof(PN532_ACK));
  queue.size += sizeof(PN532_ACK);
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                queue.bytes + queue.size, &frame_size);
  queue.size += frame_size;
  return queue;
}

struct UartReaderFixture {
  hal_uart_t uart = nullptr;
  hal_pn532_transport_t transport = nullptr;
  hal_pn532_t reader = nullptr;
};

static hal_status_t begin_uart_reader(UartReaderFixture *fixture) {
  if (fixture == nullptr) {
    return HAL_EINVAL;
  }
  *fixture = {};
  fixture->uart = hal_uart_create(HAL_UART_PORT_1, 1u, 2u);
  if (fixture->uart == nullptr) {
    return HAL_ENOMEM;
  }

  hal_status_t status = hal_pn532_transport_create_uart_handle(
      fixture->uart, HAL_PN532_PIN_NONE, &fixture->transport);
  if (status == HAL_OK) {
    status = hal_pn532_create(fixture->transport, &fixture->reader);
  }
  if (status == HAL_OK) {
    status = hal_pn532_begin(fixture->reader);
  }
  if (status == HAL_OK) {
    return HAL_OK;
  }

  if (fixture->reader != nullptr) {
    (void)hal_pn532_destroy(fixture->reader);
  }
  if (fixture->transport != nullptr) {
    (void)hal_pn532_transport_destroy(fixture->transport);
  }
  hal_uart_destroy(fixture->uart);
  *fixture = {};
  return status;
}

static hal_status_t queue_uart_response(hal_uart_t uart, uint8_t command,
                                        const uint8_t *payload,
                                        size_t payload_size) {
  uint8_t response[32] = {};
  if (uart == nullptr || payload == nullptr ||
      payload_size > sizeof(response) - 9u) {
    return HAL_EINVAL;
  }
  size_t response_size = 0u;
  make_response(command, payload, payload_size, response, &response_size);
  uint8_t queue[sizeof(PN532_ACK) + sizeof(response)] = {};
  std::memcpy(queue, PN532_ACK, sizeof(PN532_ACK));
  std::memcpy(queue + sizeof(PN532_ACK), response, response_size);
  hal_mock_uart_push(uart, queue, (int)(sizeof(PN532_ACK) + response_size));
  return HAL_OK;
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

void test_pn532_read_passive_target_accepts_four_byte_uid(void) {
  const uint8_t expected_uid[] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
  const uint8_t payload[] = {0x01u, 0x01u, 0x04u, 0x00u, 0x08u,
                             0x04u, 0xDEu, 0xADu, 0xBEu, 0xEFu};
  assert_passive_target_uid(payload, sizeof(payload), expected_uid,
                            sizeof(expected_uid));
}

void test_pn532_read_passive_target_accepts_seven_byte_uid(void) {
  const uint8_t expected_uid[] = {0x04u, 0x11u, 0x22u, 0x33u,
                                  0x44u, 0x55u, 0x66u};
  const uint8_t payload[] = {0x01u, 0x01u, 0x44u, 0x00u, 0x00u, 0x07u, 0x04u,
                             0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
  assert_passive_target_uid(payload, sizeof(payload), expected_uid,
                            sizeof(expected_uid));
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

void test_pn532_rejects_invalid_response_header_with_bounded_recovery(void) {
  static const uint8_t ack[] = {0x00u, 0x00u, 0xFFu, 0x00u, 0xFFu, 0x00u};

  {
    const uint8_t invalid_lcs[] = {0x00u, 0x00u, 0xFFu, 0xFFu, 0x00u};
    uint8_t queue[sizeof(ack) + sizeof(invalid_lcs)] = {};
    std::memcpy(queue, ack, sizeof(ack));
    std::memcpy(queue + sizeof(ack), invalid_lcs, sizeof(invalid_lcs));
    FakePN532Bus bus;
    bus.queue(queue, sizeof(queue));
    PN532 nfc(&bus);
    uint32_t version = 0u;

    TEST_ASSERT_EQUAL_INT(HAL_EPROTO, nfc.getFirmwareVersion(&version));
    TEST_ASSERT_EQUAL_INT(3, bus.readCalls);
    TEST_ASSERT_EQUAL_size_t(sizeof(queue), bus.readOffset);
  }

  {
    const uint8_t short_frame[] = {0x00u, 0x00u, 0xFFu, 0x01u, 0xFFu};
    uint8_t queue[sizeof(ack) + sizeof(short_frame)] = {};
    std::memcpy(queue, ack, sizeof(ack));
    std::memcpy(queue + sizeof(ack), short_frame, sizeof(short_frame));
    FakePN532Bus bus;
    bus.queue(queue, sizeof(queue));
    PN532 nfc(&bus);
    uint32_t version = 0u;

    TEST_ASSERT_EQUAL_INT(HAL_EPROTO, nfc.getFirmwareVersion(&version));
    TEST_ASSERT_EQUAL_INT(3, bus.readCalls);
    TEST_ASSERT_EQUAL_size_t(sizeof(queue), bus.readOffset);
  }
}

void test_pn532_bad_lcs_does_not_poison_next_uart_response(void) {
  const FirmwareRecoveryQueue queue = make_bad_lcs_recovery_queue();

  FakePN532Bus bus;
  bus.queue(queue.bytes, queue.size);
  PN532 nfc(&bus);
  uint32_t version = 0u;

  TEST_ASSERT_EQUAL_INT(HAL_EPROTO, nfc.getFirmwareVersion(&version));
  TEST_ASSERT_EQUAL_INT(HAL_OK, nfc.getFirmwareVersion(&version));
  TEST_ASSERT_EQUAL_UINT32(0x32010607u, version);
  TEST_ASSERT_EQUAL_size_t(queue.size, bus.readOffset);
}

void test_pn532_bad_lcs_recovers_through_uart_transport(void) {
  const FirmwareRecoveryQueue queue = make_bad_lcs_recovery_queue();
  UartReaderFixture fixture;
  TEST_ASSERT_EQUAL_INT(HAL_OK, begin_uart_reader(&fixture));
  hal_mock_uart_push(fixture.uart, queue.bytes, (int)queue.size);

  uint32_t version = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, hal_pn532_get_firmware_version(fixture.reader, &version));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_pn532_get_firmware_version(fixture.reader, &version));
  TEST_ASSERT_EQUAL_UINT32(0x32010607u, version);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(fixture.reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(fixture.transport));
  hal_uart_destroy(fixture.uart);
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

void test_pn532_spi_write_failure_releases_device(void) {
  hal_spi_init(1, 8, 11, 10);
  PN532_SPI bus(/*chipSelectPin=*/22, /*resetPin=*/PN532_UNUSED_PIN,
                /*bus=*/1);
  const uint8_t command[] = {PN532_COMMAND_SAMCONFIGURATION};

  hal_mock_spi_fail_next_write(1, true);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, bus.writeCommand(command, sizeof(command)));
  TEST_ASSERT_TRUE(hal_mock_gpio_get_state(22));
  TEST_ASSERT_FALSE(hal_mock_spi_transaction_active(1));
  TEST_ASSERT_EQUAL_INT(0, hal_mock_spi_get_lock_depth(1));
}

void test_pn532_spi_oversized_frame_does_not_poison_next_frame(void) {
  hal_spi_init(1u, 8u, 11u, 10u);
  PN532_SPI bus(/*chipSelectPin=*/22u, /*resetPin=*/PN532_UNUSED_PIN,
                /*bus=*/1u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, bus.begin());

  uint8_t oversized[262] = {};
  size_t oversized_size = 0u;
  make_oversized_response(PN532_COMMAND_GETFIRMWAREVERSION, oversized,
                          &oversized_size);
  uint8_t spi_rx[263] = {};
  std::memcpy(spi_rx + 1u, oversized, oversized_size);
  hal_mock_spi_push_rx(1u, spi_rx, sizeof(spi_rx));
  uint8_t received[PN532_PACKETBUFFER_SIZE] = {};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        read_transport_frame(&bus,
                                             PN532_COMMAND_GETFIRMWAREVERSION,
                                             received, sizeof(received)));

  const uint8_t payload[] = {0x32u, 0x01u, 0x06u, 0x07u};
  uint8_t valid[20] = {};
  size_t valid_size = 0u;
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                valid, &valid_size);
  std::memset(spi_rx, 0, sizeof(spi_rx));
  std::memcpy(spi_rx + 1u, valid, valid_size);
  hal_mock_spi_push_rx(1u, spi_rx, sizeof(spi_rx));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, read_transport_frame(&bus, PN532_COMMAND_GETFIRMWAREVERSION,
                                   received, sizeof(received)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(valid, received, valid_size);
}

void test_pn532_spi_cache_lifecycle_prevents_stale_tail_and_exhaustion(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_spi_init(1u, 8u, 11u, 10u));
  const uint8_t malformed_frame[13] = {0x00u, 0x00u, 0xFFu, 0x06u, 0x00u,
                                       0xD5u, 0x03u, 0xAAu, 0xBBu, 0xCCu,
                                       0xDDu, 0x00u, 0x00u};
  const uint8_t payload[] = {0x32u, 0x01u, 0x06u, 0x07u};
  uint8_t valid_frame[20] = {};
  size_t valid_frame_size = 0u;
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                valid_frame, &valid_frame_size);

  alignas(PN532_SPI) uint8_t reused_storage[sizeof(PN532_SPI)] = {};
  auto *first = new (reused_storage)
      PN532_SPI(/*chipSelectPin=*/22u, /*resetPin=*/PN532_UNUSED_PIN,
                /*bus=*/1u);
  uint8_t spi_rx[263] = {};
  std::memcpy(spi_rx + 1u, malformed_frame, sizeof(malformed_frame));
  hal_mock_spi_push_rx(1u, spi_rx, sizeof(spi_rx));
  uint8_t header[5] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, first->readData(header, sizeof(header)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(malformed_frame, header, sizeof(header));

  TEST_ASSERT_EQUAL_INT(HAL_OK, first->begin());
  std::memset(spi_rx, 0, sizeof(spi_rx));
  std::memcpy(spi_rx + 1u, valid_frame, valid_frame_size);
  hal_mock_spi_push_rx(1u, spi_rx, sizeof(spi_rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, first->readData(header, sizeof(header)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(valid_frame, header, sizeof(header));
  first->~PN532_SPI();

  auto *second = new (reused_storage)
      PN532_SPI(/*chipSelectPin=*/22u, /*resetPin=*/PN532_UNUSED_PIN,
                /*bus=*/1u);
  std::memset(spi_rx, 0, sizeof(spi_rx));
  std::memcpy(spi_rx + 1u, malformed_frame, sizeof(malformed_frame));
  hal_mock_spi_push_rx(1u, spi_rx, sizeof(spi_rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, second->readData(header, sizeof(header)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(malformed_frame, header, sizeof(header));
  second->~PN532_SPI();

  alignas(PN532_SPI) uint8_t distinct_storage[HAL_PN532_MAX_TRANSPORTS + 1u]
                                             [sizeof(PN532_SPI)] = {};
  for (size_t i = 0u; i < HAL_PN532_MAX_TRANSPORTS + 1u; ++i) {
    auto *transport = new (distinct_storage[i])
        PN532_SPI(/*chipSelectPin=*/22u, /*resetPin=*/PN532_UNUSED_PIN,
                  /*bus=*/1u);
    std::memset(spi_rx, 0, sizeof(spi_rx));
    std::memcpy(spi_rx + 1u, malformed_frame, sizeof(malformed_frame));
    hal_mock_spi_push_rx(1u, spi_rx, sizeof(spi_rx));
    TEST_ASSERT_EQUAL_INT(HAL_OK, transport->readData(header, sizeof(header)));
    transport->~PN532_SPI();
  }
}

void test_pn532_i2c_oversized_frame_does_not_poison_next_frame(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_i2c_init_bus(1u, 4u, 5u, HAL_I2C_CLOCK_STANDARD_HZ));
  PN532_I2C bus(/*resetPin=*/PN532_UNUSED_PIN,
                /*address=*/PN532_I2C_DEFAULT_ADDRESS, /*bus=*/1u);

  uint8_t oversized[262] = {};
  size_t oversized_size = 0u;
  make_oversized_response(PN532_COMMAND_GETFIRMWAREVERSION, oversized,
                          &oversized_size);
  uint8_t i2c_rx[UINT8_MAX] = {0x01u};
  std::memcpy(i2c_rx + 1u, oversized, sizeof(i2c_rx) - 1u);
  hal_mock_i2c_inject_rx_bus(1u, i2c_rx, sizeof(i2c_rx));
  uint8_t received[PN532_PACKETBUFFER_SIZE] = {};
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        read_transport_frame(&bus,
                                             PN532_COMMAND_GETFIRMWAREVERSION,
                                             received, sizeof(received)));

  const uint8_t payload[] = {0x32u, 0x01u, 0x06u, 0x07u};
  uint8_t valid[20] = {};
  size_t valid_size = 0u;
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                valid, &valid_size);
  std::memset(i2c_rx, 0, sizeof(i2c_rx));
  i2c_rx[0] = 0x01u;
  std::memcpy(i2c_rx + 1u, valid, valid_size);
  hal_mock_i2c_inject_rx_bus(1u, i2c_rx, sizeof(i2c_rx));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, read_transport_frame(&bus, PN532_COMMAND_GETFIRMWAREVERSION,
                                   received, sizeof(received)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(valid, received, valid_size);
  hal_i2c_deinit_bus(1u);
}

void test_pn532_i2c_cache_lifecycle_prevents_stale_tail_and_exhaustion(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_i2c_init_bus(1u, 4u, 5u, HAL_I2C_CLOCK_STANDARD_HZ));
  const uint8_t malformed_frame[13] = {0x00u, 0x00u, 0xFFu, 0x06u, 0x00u,
                                       0xD5u, 0x03u, 0xAAu, 0xBBu, 0xCCu,
                                       0xDDu, 0x00u, 0x00u};
  const uint8_t payload[] = {0x32u, 0x01u, 0x06u, 0x07u};
  uint8_t valid_frame[20] = {};
  size_t valid_frame_size = 0u;
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                valid_frame, &valid_frame_size);

  alignas(PN532_I2C) uint8_t reused_storage[sizeof(PN532_I2C)] = {};
  auto *first = new (reused_storage)
      PN532_I2C(/*resetPin=*/PN532_UNUSED_PIN,
                /*address=*/PN532_I2C_DEFAULT_ADDRESS, /*bus=*/1u);
  uint8_t i2c_rx[UINT8_MAX] = {0x01u};
  std::memcpy(i2c_rx + 1u, malformed_frame, sizeof(malformed_frame));
  hal_mock_i2c_inject_rx_bus(1u, i2c_rx, sizeof(i2c_rx));
  uint8_t header[5] = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, first->readData(header, sizeof(header)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(malformed_frame, header, sizeof(header));

  TEST_ASSERT_EQUAL_INT(HAL_OK, first->begin());
  std::memset(i2c_rx, 0, sizeof(i2c_rx));
  i2c_rx[0] = 0x01u;
  std::memcpy(i2c_rx + 1u, valid_frame, valid_frame_size);
  hal_mock_i2c_inject_rx_bus(1u, i2c_rx, sizeof(i2c_rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, first->readData(header, sizeof(header)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(valid_frame, header, sizeof(header));
  first->~PN532_I2C();

  auto *second = new (reused_storage)
      PN532_I2C(/*resetPin=*/PN532_UNUSED_PIN,
                /*address=*/PN532_I2C_DEFAULT_ADDRESS, /*bus=*/1u);
  std::memset(i2c_rx, 0, sizeof(i2c_rx));
  i2c_rx[0] = 0x01u;
  std::memcpy(i2c_rx + 1u, malformed_frame, sizeof(malformed_frame));
  hal_mock_i2c_inject_rx_bus(1u, i2c_rx, sizeof(i2c_rx));
  TEST_ASSERT_EQUAL_INT(HAL_OK, second->readData(header, sizeof(header)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(malformed_frame, header, sizeof(header));
  second->~PN532_I2C();

  alignas(PN532_I2C) uint8_t distinct_storage[HAL_PN532_MAX_TRANSPORTS + 1u]
                                             [sizeof(PN532_I2C)] = {};
  for (size_t i = 0u; i < HAL_PN532_MAX_TRANSPORTS + 1u; ++i) {
    auto *transport = new (distinct_storage[i])
        PN532_I2C(/*resetPin=*/PN532_UNUSED_PIN,
                  /*address=*/PN532_I2C_DEFAULT_ADDRESS, /*bus=*/1u);
    std::memset(i2c_rx, 0, sizeof(i2c_rx));
    i2c_rx[0] = 0x01u;
    std::memcpy(i2c_rx + 1u, malformed_frame, sizeof(malformed_frame));
    hal_mock_i2c_inject_rx_bus(1u, i2c_rx, sizeof(i2c_rx));
    TEST_ASSERT_EQUAL_INT(HAL_OK, transport->readData(header, sizeof(header)));
    transport->~PN532_I2C();
  }
  hal_i2c_deinit_bus(1u);
}

void test_pn532_c_facade_propagates_i2c_bus_error(void) {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_i2c_init_bus(0u, 4u, 5u, HAL_I2C_CLOCK_STANDARD_HZ));
  const hal_pn532_i2c_config_t config = hal_pn532_i2c_default_config();
  hal_pn532_transport_t transport = nullptr;
  hal_pn532_t reader = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_pn532_transport_create_i2c(&config, &transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_create(transport, &reader));

  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_pn532_wakeup(reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_begin(reader));
  hal_mock_i2c_set_busy_bus(0u, true);
  const hal_status_t fault_status = hal_pn532_wakeup(reader);
  hal_mock_i2c_set_busy_bus(0u, false);
  const hal_status_t recovery_status = hal_pn532_wakeup(reader);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(transport));
  hal_i2c_deinit_bus(0u);
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, fault_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, recovery_status);
}

void test_pn532_c_facade_propagates_uart_first_error(void) {
  hal_uart_t uart = hal_uart_create(HAL_UART_PORT_1, 1u, 2u);
  TEST_ASSERT_NOT_NULL(uart);
  hal_pn532_transport_t transport = nullptr;
  hal_pn532_t reader = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_create_uart_handle(
                                    uart, HAL_PN532_PIN_NONE, &transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_create(transport, &reader));

  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, hal_pn532_wakeup(reader));
  hal_mock_uart_set_next_begin_status(uart, HAL_EHW);
  const hal_status_t begin_fault_status = hal_pn532_begin(reader);
  const hal_status_t begin_recovery_status = hal_pn532_begin(reader);

  hal_mock_uart_set_next_write_status(uart, HAL_EIO);
  hal_mock_uart_set_next_flush_status(uart, HAL_EBUS);
  const hal_status_t first_write_status = hal_pn532_wakeup(reader);
  const hal_status_t consumed_flush_status = hal_pn532_wakeup(reader);

  hal_mock_uart_set_next_flush_status(uart, HAL_EBUS);
  const hal_status_t flush_status = hal_pn532_wakeup(reader);

  hal_mock_uart_set_next_begin_status(uart, HAL_EHW);
  const hal_status_t rebegin_fault_status = hal_pn532_begin(reader);
  const hal_status_t state_after_failed_rebegin = hal_pn532_wakeup(reader);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(transport));
  hal_uart_destroy(uart);
  TEST_ASSERT_EQUAL_INT(HAL_EHW, begin_fault_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, begin_recovery_status);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, first_write_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, consumed_flush_status);
  TEST_ASSERT_EQUAL_INT(HAL_EBUS, flush_status);
  TEST_ASSERT_EQUAL_INT(HAL_EHW, rebegin_fault_status);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, state_after_failed_rebegin);
}

void test_pn532_c_facade_rejects_stale_handles_after_reuse(void) {
  const hal_pn532_spi_config_t config = hal_pn532_spi_default_config(22u);
  hal_pn532_transport_t first_transport = nullptr;
  hal_pn532_t first_reader = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_pn532_transport_create_spi(&config, &first_transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_pn532_create(first_transport, &first_reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(first_reader));

  hal_pn532_t second_reader = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_pn532_create(first_transport, &second_reader));
  const hal_status_t stale_reader_status = hal_pn532_begin(first_reader);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(second_reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(first_transport));

  hal_pn532_transport_t second_transport = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_pn532_transport_create_spi(&config, &second_transport));
  const hal_status_t stale_transport_status =
      hal_pn532_transport_destroy(first_transport);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(second_transport));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, stale_reader_status);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, stale_transport_status);
}

void test_pn532_uart_overflow_drains_frame_before_next_response(void) {
  static const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  uint8_t oversized_payload[56] = {};
  uint8_t oversized_frame[sizeof(oversized_payload) + 9u] = {};
  size_t oversized_frame_len = 0u;
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, oversized_payload,
                sizeof(oversized_payload), oversized_frame,
                &oversized_frame_len);

  const uint8_t valid_payload[] = {0x32u, 0x01u, 0x06u, 0x07u};
  uint8_t valid_frame[sizeof(valid_payload) + 9u] = {};
  size_t valid_frame_len = 0u;
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, valid_payload,
                sizeof(valid_payload), valid_frame, &valid_frame_len);

  uint8_t queue[sizeof(ack) * 2u + sizeof(oversized_frame) +
                sizeof(valid_frame)] = {};
  size_t queue_len = 0u;
  std::memcpy(queue + queue_len, ack, sizeof(ack));
  queue_len += sizeof(ack);
  std::memcpy(queue + queue_len, oversized_frame, oversized_frame_len);
  queue_len += oversized_frame_len;
  std::memcpy(queue + queue_len, ack, sizeof(ack));
  queue_len += sizeof(ack);
  std::memcpy(queue + queue_len, valid_frame, valid_frame_len);
  queue_len += valid_frame_len;

  hal_uart_t uart = hal_uart_create(HAL_UART_PORT_1, 1u, 2u);
  TEST_ASSERT_NOT_NULL(uart);
  hal_pn532_transport_t transport = nullptr;
  hal_pn532_t reader = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_create_uart_handle(
                                    uart, HAL_PN532_PIN_NONE, &transport));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_create(transport, &reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_begin(reader));
  hal_mock_uart_push(uart, queue, (int)queue_len);

  uint32_t version = 0u;
  const hal_status_t overflow_status =
      hal_pn532_get_firmware_version(reader, &version);
  const hal_status_t next_status =
      hal_pn532_get_firmware_version(reader, &version);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(transport));
  hal_uart_destroy(uart);
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, overflow_status);
  TEST_ASSERT_EQUAL_INT(HAL_OK, next_status);
  TEST_ASSERT_EQUAL_UINT32(0x32010607u, version);
}

void test_pn532_ultralight_read_copies_first_page_from_full_uart_response(
    void) {
  const uint8_t payload[] = {0x00u, 0x10u, 0x11u, 0x12u, 0x13u, 0x20u,
                             0x21u, 0x22u, 0x23u, 0x30u, 0x31u, 0x32u,
                             0x33u, 0x40u, 0x41u, 0x42u, 0x43u};
  UartReaderFixture fixture;
  TEST_ASSERT_EQUAL_INT(HAL_OK, begin_uart_reader(&fixture));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, queue_uart_response(fixture.uart, PN532_COMMAND_INDATAEXCHANGE,
                                  payload, sizeof(payload)));

  struct {
    uint8_t before;
    uint8_t page[HAL_PN532_PAGE_SIZE];
    uint8_t after;
  } guarded = {0xA5u, {}, 0x5Au};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_mifare_ultralight_read(
                                    fixture.reader, 4u, guarded.page));
  const uint8_t expected[] = {0x10u, 0x11u, 0x12u, 0x13u};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, guarded.page, sizeof(expected));
  TEST_ASSERT_EQUAL_HEX8(0xA5u, guarded.before);
  TEST_ASSERT_EQUAL_HEX8(0x5Au, guarded.after);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(fixture.reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(fixture.transport));
  hal_uart_destroy(fixture.uart);
}

void test_pn532_classic_read_rejects_short_uart_payload(void) {
  const uint8_t payload[] = {0x00u, 0x10u, 0x11u, 0x12u, 0x13u};
  UartReaderFixture fixture;
  TEST_ASSERT_EQUAL_INT(HAL_OK, begin_uart_reader(&fixture));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, queue_uart_response(fixture.uart, PN532_COMMAND_INDATAEXCHANGE,
                                  payload, sizeof(payload)));

  uint8_t block[HAL_PN532_MIFARE_BLOCK_SIZE];
  std::memset(block, 0xA5, sizeof(block));
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, hal_pn532_mifare_classic_read(fixture.reader, 1u, block));
  uint8_t unchanged[HAL_PN532_MIFARE_BLOCK_SIZE];
  std::memset(unchanged, 0xA5, sizeof(unchanged));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(unchanged, block, sizeof(block));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_destroy(fixture.reader));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_pn532_transport_destroy(fixture.transport));
  hal_uart_destroy(fixture.uart);
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

void test_pn532_check_response_frame_requires_postamble(void) {
  uint8_t frame[20] = {};
  size_t frame_len = 0u;
  const uint8_t payload[] = {0x32u, 0x01u, 0x06u, 0x07u};
  make_response(PN532_COMMAND_GETFIRMWAREVERSION, payload, sizeof(payload),
                frame, &frame_len);

  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, PN532::checkResponseFrame(
                                           frame, frame_len - 1u,
                                           PN532_COMMAND_GETFIRMWAREVERSION));
  frame[frame_len - 1u] = 0x01u;
  TEST_ASSERT_EQUAL_INT(
      HAL_EPROTO, PN532::checkResponseFrame(frame, frame_len,
                                            PN532_COMMAND_GETFIRMWAREVERSION));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pn532_command_frame_matches_reference_layout);
  RUN_TEST(test_pn532_get_firmware_version_parses_response);
  RUN_TEST(test_pn532_read_passive_target_accepts_four_byte_uid);
  RUN_TEST(test_pn532_read_passive_target_accepts_seven_byte_uid);
  RUN_TEST(test_pn532_rejects_bad_ack);
  RUN_TEST(test_pn532_rejects_invalid_response_header_with_bounded_recovery);
  RUN_TEST(test_pn532_bad_lcs_does_not_poison_next_uart_response);
  RUN_TEST(test_pn532_bad_lcs_recovers_through_uart_transport);
  RUN_TEST(test_pn532_spi_status_read_uses_hal_transaction);
  RUN_TEST(test_pn532_spi_write_failure_releases_device);
  RUN_TEST(test_pn532_spi_oversized_frame_does_not_poison_next_frame);
  RUN_TEST(test_pn532_spi_cache_lifecycle_prevents_stale_tail_and_exhaustion);
  RUN_TEST(test_pn532_i2c_oversized_frame_does_not_poison_next_frame);
  RUN_TEST(test_pn532_i2c_cache_lifecycle_prevents_stale_tail_and_exhaustion);
  RUN_TEST(test_pn532_c_facade_propagates_i2c_bus_error);
  RUN_TEST(test_pn532_c_facade_propagates_uart_first_error);
  RUN_TEST(test_pn532_c_facade_rejects_stale_handles_after_reuse);
  RUN_TEST(test_pn532_uart_overflow_drains_frame_before_next_response);
  RUN_TEST(
      test_pn532_ultralight_read_copies_first_page_from_full_uart_response);
  RUN_TEST(test_pn532_classic_read_rejects_short_uart_payload);
  RUN_TEST(test_pn532_check_response_frame_detects_checksum_error);
  RUN_TEST(test_pn532_check_response_frame_requires_postamble);
  return UNITY_END();
}
