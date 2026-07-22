#include "hal/impl/.mock/hal_mock.h"
#include "hal/impl/shared/frameworks/BearSSL/jh_bearssl_bsd_io.h"
#include "hal/impl/shared/frameworks/BearSSL/jh_bearssl_engine.h"
#include "hal/impl/shared/frameworks/BearSSL/jh_bearssl_provider.h"
#include "utils/unity.h"

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool s_cancelled;
static unsigned s_service_calls;

void setUp(void) {
  hal_mock_bsd_sockets_reset();
  hal_mock_net_reset();
  hal_mock_tcp_reset();
  s_cancelled = false;
  s_service_calls = 0u;
}

void tearDown(void) { hal_mock_bsd_sockets_reset(); }

static int connected_socket(void) {
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);
  struct sockaddr_in remote = {};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(443u);
  TEST_ASSERT_EQUAL_INT(1, inet_pton(AF_INET, "192.0.2.10", &remote.sin_addr));
  TEST_ASSERT_EQUAL_INT(
      0, connect(fd, reinterpret_cast<const struct sockaddr *>(&remote),
                 (socklen_t)sizeof(remote)));
  return fd;
}

static bool cancelled(void *) { return s_cancelled; }
static void service(void *) { ++s_service_calls; }

void test_blocking_callbacks_preserve_partial_io_and_cancellation(void) {
  const int fd = connected_socket();
  hal_tcp_socket_t tcp = hal_mock_bsd_socket_get_tcp_handle(fd);
  TEST_ASSERT_NOT_NULL(tcp);

  jh_bearssl_bsd_io_t io = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bearssl_bsd_io_init(&io, fd, 20u, cancelled,
                                                       service, nullptr));
  static const uint8_t incoming[] = {1u, 2u, 3u};
  hal_mock_tcp_inject_rx(tcp, incoming, (uint16_t)sizeof(incoming));
  uint8_t received[8] = {};
  TEST_ASSERT_EQUAL_INT((int)sizeof(incoming),
                        jh_bearssl_bsd_read(&io, received, sizeof(received)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(incoming, received, sizeof(incoming));

  uint8_t outgoing[700] = {};
  TEST_ASSERT_EQUAL_INT(512,
                        jh_bearssl_bsd_write(&io, outgoing, sizeof(outgoing)));
  TEST_ASSERT_EQUAL_UINT16(512u, hal_mock_tcp_get_last_tx_len(tcp));

  s_cancelled = true;
  TEST_ASSERT_EQUAL_INT(-1,
                        jh_bearssl_bsd_read(&io, received, sizeof(received)));
  TEST_ASSERT_EQUAL_INT(HAL_ECANCELED, io.last_status);
  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_blocking_callback_timeout_is_finite_and_services_runtime(void) {
  const int fd = connected_socket();
  jh_bearssl_bsd_io_t io = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bearssl_bsd_io_init(&io, fd, 3u, cancelled, service, nullptr));
  uint8_t byte = 0u;
  TEST_ASSERT_EQUAL_INT(-1, jh_bearssl_bsd_read(&io, &byte, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, io.last_status);
  TEST_ASSERT_GREATER_THAN_UINT(0u, s_service_calls);
  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

typedef struct {
  unsigned state;
  unsigned char bytes[1024];
  size_t length;
  size_t acknowledged;
} fake_engine_t;

static unsigned fake_state(const void *context) {
  const fake_engine_t *engine = static_cast<const fake_engine_t *>(context);
  return engine->acknowledged < engine->length ? engine->state : 0x0008u;
}
static int32_t fake_error(const void *) { return 0; }
static unsigned char *fake_send_buffer(void *context, size_t *length) {
  fake_engine_t *engine = static_cast<fake_engine_t *>(context);
  *length = engine->length - engine->acknowledged;
  return engine->bytes + engine->acknowledged;
}
static void fake_send_ack(void *context, size_t length) {
  static_cast<fake_engine_t *>(context)->acknowledged += length;
}
static unsigned char *fake_receive_buffer(void *context, size_t *length) {
  return fake_send_buffer(context, length);
}
static void fake_receive_ack(void *context, size_t length) {
  fake_send_ack(context, length);
}

void test_poll_engine_honours_step_budget_and_partial_send(void) {
  const int fd = connected_socket();
  fake_engine_t engine = {};
  engine.state = 0x0002u;
  engine.length = sizeof(engine.bytes);
  const jh_bearssl_engine_ops_t ops = {fake_state,          fake_error,
                                       fake_send_buffer,    fake_send_ack,
                                       fake_receive_buffer, fake_receive_ack};
  jh_bearssl_poll_result_t result = {};

  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, jh_bearssl_engine_poll_with_ops(
                                        &engine, &ops, fd, 1u, &result));
  TEST_ASSERT_EQUAL_UINT16(1u, result.steps);
  TEST_ASSERT_EQUAL_UINT32(512u, engine.acknowledged);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bearssl_engine_poll_with_ops(&engine, &ops, fd, 1u, &result));
  TEST_ASSERT_EQUAL_UINT16(1u, result.steps);
  TEST_ASSERT_EQUAL_UINT32(sizeof(engine.bytes), engine.acknowledged);
  TEST_ASSERT_EQUAL_INT(JH_BEARSSL_EVENT_APPLICATION_WRITABLE, result.event);
  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_read_poll_does_not_let_sendapp_starve_received_records(void) {
  const int fd = connected_socket();
  hal_tcp_socket_t tcp = hal_mock_bsd_socket_get_tcp_handle(fd);
  TEST_ASSERT_NOT_NULL(tcp);
  static const uint8_t incoming[] = {0x16u, 0x03u, 0x03u};
  hal_mock_tcp_inject_rx(tcp, incoming, (uint16_t)sizeof(incoming));

  fake_engine_t engine = {};
  engine.state = 0x0004u | 0x0008u;
  engine.length = sizeof(incoming);
  const jh_bearssl_engine_ops_t ops = {fake_state,          fake_error,
                                       fake_send_buffer,    fake_send_ack,
                                       fake_receive_buffer, fake_receive_ack};
  jh_bearssl_poll_result_t result = {};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bearssl_engine_poll_for_read_with_ops(
                                    &engine, &ops, fd, 1u, &result));
  TEST_ASSERT_EQUAL_UINT16(1u, result.steps);
  TEST_ASSERT_EQUAL_UINT32(sizeof(incoming), engine.acknowledged);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(incoming, engine.bytes, sizeof(incoming));
  TEST_ASSERT_EQUAL_INT(JH_BEARSSL_EVENT_APPLICATION_WRITABLE, result.event);
  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_provider_initializes_real_br_sslio_without_arduino_wrapper(void) {
  const int fd = connected_socket();
  br_ssl_engine_context engine = {};
  jh_bearssl_blocking_io_t provider = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_bearssl_blocking_io_init(&provider, &engine,
                                                            fd, 10u, cancelled,
                                                            service, nullptr));
  TEST_ASSERT_EQUAL_STRING("aca13833b6f9ddffaea2041a01facc76829dc03b",
                           jh_bearssl_provider_source_revision());
  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_secure_client_requires_complete_anchor_time_and_entropy(void) {
  static const uint8_t dn[] = {0x30u, 0x00u};
  static const uint8_t modulus[] = {0x01u, 0x02u, 0x03u};
  static const uint8_t exponent[] = {0x01u, 0x00u, 0x01u};
  hal_tls_trust_anchor_t anchor = {};
  anchor.subject_dn = dn;
  anchor.subject_dn_length = sizeof(dn);
  anchor.key_type = HAL_TLS_TRUST_KEY_RSA;
  anchor.key.rsa.modulus = modulus;
  anchor.key.rsa.modulus_length = sizeof(modulus);
  anchor.key.rsa.exponent = exponent;
  anchor.key.rsa.exponent_length = sizeof(exponent);
  uint8_t entropy[JH_BEARSSL_ENTROPY_SIZE] = {};
  memset(entropy, 0xA5, sizeof(entropy));
  jh_bearssl_client_t provider = {};

  TEST_ASSERT_EQUAL_INT(
      HAL_ECONFIG, jh_bearssl_client_init(&provider, &anchor, 1u, "example.com",
                                          HAL_TLS_MIN_VALID_UNIX_TIME - 1u,
                                          entropy, sizeof(entropy)));
  TEST_ASSERT_EQUAL_INT(
      HAL_ECONFIG, jh_bearssl_client_init(&provider, &anchor, 1u, "example.com",
                                          HAL_TLS_MIN_VALID_UNIX_TIME, entropy,
                                          sizeof(entropy) - 1u));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bearssl_client_init(&provider, &anchor, 1u, "example.com",
                                     1704067200u, entropy, sizeof(entropy)));
  TEST_ASSERT_EQUAL_STRING("example.com",
                           br_ssl_engine_get_server_name(&provider.client.eng));
  TEST_ASSERT_EQUAL_UINT32(719528u + 19723u, provider.x509.days);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_blocking_callbacks_preserve_partial_io_and_cancellation);
  RUN_TEST(test_blocking_callback_timeout_is_finite_and_services_runtime);
  RUN_TEST(test_poll_engine_honours_step_budget_and_partial_send);
  RUN_TEST(test_read_poll_does_not_let_sendapp_starve_received_records);
  RUN_TEST(test_provider_initializes_real_br_sslio_without_arduino_wrapper);
  RUN_TEST(test_secure_client_requires_complete_anchor_time_and_entropy);
  return UNITY_END();
}
