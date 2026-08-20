#include "hal/impl/.mock/hal_mock.h"
#include "hal/network/notify/hal_notify.h"
#include "utils/unity.h"

#include <atomic>
#include <stdio.h>
#include <string.h>
#include <thread>

typedef struct {
  uint32_t magic;
  uint32_t sends;
  uint32_t polls;
} fake_notify_state_t;

static const uint32_t FAKE_MAGIC = 0x4E4F5446u;
static hal_status_t s_fake_open_status = HAL_OK;
static hal_status_t s_fake_send_status = HAL_OK;
static hal_status_t s_fake_close_status = HAL_OK;
static uint32_t s_fake_open_calls = 0u;
static uint32_t s_fake_close_calls = 0u;
static char s_last_body[128] = {};
static char s_last_destination[64] = {};
static char s_last_device_name[64] = {};
static hal_notify_format_t s_last_format = HAL_NOTIFY_FORMAT_DEFAULT;
static uint32_t s_last_timeout_ms = 0u;
static std::atomic<bool> s_fake_send_wait{false};
static std::atomic<bool> s_fake_send_entered{false};
static std::atomic<bool> s_fake_send_release{false};

static hal_status_t fake_open(void *state, const void *config) {
  (void)config;
  s_fake_open_calls++;
  if (s_fake_open_status != HAL_OK) {
    return s_fake_open_status;
  }
  fake_notify_state_t *fake = static_cast<fake_notify_state_t *>(state);
  fake->magic = FAKE_MAGIC;
  return HAL_OK;
}

static hal_status_t fake_send(void *state, const hal_notify_message_t *message,
                              hal_notify_receipt_t *receipt) {
  fake_notify_state_t *fake = static_cast<fake_notify_state_t *>(state);
  TEST_ASSERT_EQUAL_UINT32(FAKE_MAGIC, fake->magic);
  fake->sends++;
  snprintf(s_last_body, sizeof(s_last_body), "%s", message->body);
  snprintf(s_last_destination, sizeof(s_last_destination), "%s",
           message->destination != NULL ? message->destination : "");
  snprintf(s_last_device_name, sizeof(s_last_device_name), "%s",
           message->device_name != NULL ? message->device_name : "");
  s_last_format = message->format;
  s_last_timeout_ms = message->timeout_ms;
  if (s_fake_send_wait.load(std::memory_order_acquire)) {
    s_fake_send_entered.store(true, std::memory_order_release);
    while (!s_fake_send_release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }
  if (receipt != NULL) {
    receipt->provider_status = 202;
    snprintf(receipt->provider_message_id, sizeof(receipt->provider_message_id),
             "fake-%lu", (unsigned long)fake->sends);
  }
  return s_fake_send_status;
}

static hal_status_t fake_poll(void *state) {
  fake_notify_state_t *fake = static_cast<fake_notify_state_t *>(state);
  TEST_ASSERT_EQUAL_UINT32(FAKE_MAGIC, fake->magic);
  fake->polls++;
  return HAL_OK;
}

static hal_status_t fake_close(void *state) {
  fake_notify_state_t *fake = static_cast<fake_notify_state_t *>(state);
  if (fake->magic == FAKE_MAGIC) {
    s_fake_close_calls++;
  }
  memset(fake, 0, sizeof(*fake));
  return s_fake_close_status;
}

static const hal_notify_backend_t FAKE_BACKEND = {
    HAL_NOTIFY_BACKEND_API_VERSION,
    "fake",
    sizeof(fake_notify_state_t),
    fake_open,
    fake_send,
    fake_poll,
    fake_close,
};

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_net_reset();
  hal_mock_tcp_reset();
  s_fake_open_status = HAL_OK;
  s_fake_send_status = HAL_OK;
  s_fake_close_status = HAL_OK;
  s_fake_open_calls = 0u;
  s_fake_close_calls = 0u;
  memset(s_last_body, 0, sizeof(s_last_body));
  memset(s_last_destination, 0, sizeof(s_last_destination));
  memset(s_last_device_name, 0, sizeof(s_last_device_name));
  s_last_format = HAL_NOTIFY_FORMAT_DEFAULT;
  s_last_timeout_ms = 0u;
  s_fake_send_wait.store(false, std::memory_order_relaxed);
  s_fake_send_entered.store(false, std::memory_order_relaxed);
  s_fake_send_release.store(false, std::memory_order_relaxed);
}

void tearDown(void) {}

static void init_local_telegram_config(hal_notify_telegram_config_t *telegram,
                                       hal_notify_config_t *config) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_telegram_config_init(telegram));
  telegram->bot_token = "123456:ABC_DEF";
  telegram->default_chat_id = "42";
  telegram->api_host = "localhost";
  telegram->port = 18080u;
  telegram->transport = HAL_NOTIFY_TRANSPORT_HTTP;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_config_init(config));
  config->backend = hal_notify_telegram_backend();
  config->backend_config = telegram;
}

void test_config_message_and_backend_validation(void) {
  hal_notify_config_t config = {};
  hal_notify_message_t message = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_config_init(&config));
  TEST_ASSERT_EQUAL_UINT32(HAL_NOTIFY_DEFAULT_TIMEOUT_MS,
                           config.default_timeout_ms);
  TEST_ASSERT_EQUAL_INT(HAL_NOTIFY_FORMAT_TEXT, config.default_format);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_message_init(&message));
  TEST_ASSERT_EQUAL_INT(HAL_NOTIFY_SEVERITY_INFO, message.severity);
  TEST_ASSERT_EQUAL_INT(HAL_NOTIFY_FORMAT_DEFAULT, message.format);

  hal_notify_channel_t channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_notify_open(&config, &channel));

  hal_notify_backend_t bad = FAKE_BACKEND;
  bad.api_version = 0u;
  config.backend = &bad;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_notify_open(&config, &channel));

  bad = FAKE_BACKEND;
  bad.state_size = HAL_NOTIFY_BACKEND_STATE_SIZE + 1u;
  config.backend = &bad;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_notify_open(&config, &channel));
}

void test_fake_backend_send_poll_close_and_stale_handle(void) {
  hal_notify_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_config_init(&config));
  config.backend = &FAKE_BACKEND;
  config.device_name = "garage";
  config.default_timeout_ms = 1234u;
  config.default_format = HAL_NOTIFY_FORMAT_HTML;

  hal_notify_channel_t channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channel));
  TEST_ASSERT_NOT_NULL(channel);
  TEST_ASSERT_EQUAL_UINT32(1u, s_fake_open_calls);

  hal_notify_message_t message = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_message_init(&message));
  message.destination = "ops";
  message.body = "hello";

  hal_notify_receipt_t receipt = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_send(channel, &message, &receipt));
  TEST_ASSERT_EQUAL_STRING("hello", s_last_body);
  TEST_ASSERT_EQUAL_STRING("ops", s_last_destination);
  TEST_ASSERT_EQUAL_STRING("garage", s_last_device_name);
  TEST_ASSERT_EQUAL_INT(HAL_NOTIFY_FORMAT_HTML, s_last_format);
  TEST_ASSERT_EQUAL_UINT32(1234u, s_last_timeout_ms);
  TEST_ASSERT_EQUAL_INT32(202, receipt.provider_status);
  TEST_ASSERT_EQUAL_STRING("fake-1", receipt.provider_message_id);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_poll(channel));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_close(channel));
  TEST_ASSERT_EQUAL_UINT32(1u, s_fake_close_calls);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_notify_send_text(channel, "stale"));
}

void test_close_propagates_backend_failure_and_releases_channel(void) {
  hal_notify_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_config_init(&config));
  config.backend = &FAKE_BACKEND;

  hal_notify_channel_t channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channel));
  s_fake_close_status = HAL_EIO;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, hal_notify_close(channel));
  TEST_ASSERT_EQUAL_UINT32(1u, s_fake_close_calls);

  s_fake_close_status = HAL_OK;
  channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channel));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_close(channel));
}

void test_deferred_close_is_serialized_and_reported_by_active_send(void) {
  hal_notify_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_config_init(&config));
  config.backend = &FAKE_BACKEND;

  hal_notify_channel_t channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channel));

  hal_notify_message_t message = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_message_init(&message));
  message.body = "in flight";

  s_fake_close_status = HAL_EIO;
  s_fake_send_wait.store(true, std::memory_order_release);
  std::atomic<int> send_status{HAL_ESTATE};
  std::thread sender([&]() {
    send_status.store(hal_notify_send(channel, &message, NULL),
                      std::memory_order_release);
  });
  while (!s_fake_send_entered.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  const hal_status_t close_status = hal_notify_close(channel);
  const uint32_t close_calls_while_sending = s_fake_close_calls;
  s_fake_send_release.store(true, std::memory_order_release);
  sender.join();

  TEST_ASSERT_EQUAL_INT(HAL_OK, close_status);
  TEST_ASSERT_EQUAL_UINT32(0u, close_calls_while_sending);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, send_status.load(std::memory_order_acquire));
  TEST_ASSERT_EQUAL_UINT32(1u, s_fake_close_calls);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_notify_send_text(channel, "stale"));
}

void test_channel_pool_reports_exhaustion_and_recovers_failed_open(void) {
  hal_notify_config_t config = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_config_init(&config));
  config.backend = &FAKE_BACKEND;

  s_fake_open_status = HAL_ECONFIG;
  hal_notify_channel_t failed = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, hal_notify_open(&config, &failed));
  TEST_ASSERT_NULL(failed);

  s_fake_open_status = HAL_OK;
  hal_notify_channel_t channels[HAL_NOTIFY_MAX_CHANNELS] = {};
  for (size_t index = 0u; index < HAL_NOTIFY_MAX_CHANNELS; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channels[index]));
    TEST_ASSERT_NOT_NULL(channels[index]);
  }
  hal_notify_channel_t extra = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM, hal_notify_open(&config, &extra));
  for (size_t index = 0u; index < HAL_NOTIFY_MAX_CHANNELS; ++index) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_close(channels[index]));
  }
}

void test_telegram_requires_https_for_public_api_host(void) {
  const char *hosts[] = {NULL, "api.telegram.org", "API.TELEGRAM.ORG",
                         "api.telegram.org.", "Api.Telegram.Org.."};
  for (size_t index = 0u; index < sizeof(hosts) / sizeof(hosts[0]); ++index) {
    hal_notify_telegram_config_t telegram = {};
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_telegram_config_init(&telegram));
    telegram.bot_token = "123456:ABC_DEF";
    telegram.default_chat_id = "42";
    telegram.api_host = hosts[index];
    telegram.transport = HAL_NOTIFY_TRANSPORT_HTTP;

    hal_notify_config_t config = {};
    TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_config_init(&config));
    config.backend = hal_notify_telegram_backend();
    config.backend_config = &telegram;

    hal_notify_channel_t channel = NULL;
    TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, hal_notify_open(&config, &channel));
    TEST_ASSERT_NULL(channel);
  }
}

void test_telegram_custom_http_host_uses_hal_http_client(void) {
  hal_notify_telegram_config_t telegram = {};
  hal_notify_config_t config = {};
  init_local_telegram_config(&telegram, &config);
  config.device_name = "garage";

  hal_notify_channel_t channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channel));

  const char body[] = "{\"ok\":true,\"result\":{\"message_id\":77}}";
  char reply[160] = {};
  snprintf(reply, sizeof(reply),
           "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s", strlen(body),
           body);
  hal_mock_tcp_set_next_rx(reinterpret_cast<const uint8_t *>(reply),
                           (uint16_t)strlen(reply));

  hal_notify_message_t message = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_message_init(&message));
  message.title = "ECU alert";
  message.body = "Coolant high";
  message.severity = HAL_NOTIFY_SEVERITY_ERROR;

  hal_notify_receipt_t receipt = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_send(channel, &message, &receipt));
  TEST_ASSERT_EQUAL_INT32(200, receipt.provider_status);
  TEST_ASSERT_EQUAL_STRING("77", receipt.provider_message_id);
  TEST_ASSERT_EQUAL_UINT32(1u, receipt.parts_sent);
  TEST_ASSERT_EQUAL_UINT32(1u, receipt.parts_total);

  const hal_tcp_socket_t socket = hal_mock_tcp_get_last_opened_socket();
  char sent[512] = {};
  const uint16_t sent_len = hal_mock_tcp_get_last_tx_len(socket);
  TEST_ASSERT_LESS_THAN(sizeof(sent), sent_len);
  memcpy(sent, hal_mock_tcp_get_last_tx_payload(socket), sent_len);
  TEST_ASSERT_NOT_NULL(strstr(sent, "\"chat_id\":\"42\""));
  TEST_ASSERT_NOT_NULL(
      strstr(sent, "\"text\":\"[ERROR] [garage] ECU alert\\nCoolant high\""));

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_close(channel));
}

void test_telegram_splits_long_text_and_reports_all_parts(void) {
  hal_notify_telegram_config_t telegram = {};
  hal_notify_config_t config = {};
  init_local_telegram_config(&telegram, &config);
  config.device_name = "garage";

  hal_notify_channel_t channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channel));

  const char response_body[] = "{\"ok\":true,\"result\":{\"message_id\":78}}";
  char reply[160] = {};
  snprintf(reply, sizeof(reply),
           "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",
           strlen(response_body), response_body);
  hal_mock_tcp_set_next_rx(reinterpret_cast<const uint8_t *>(reply),
                           (uint16_t)strlen(reply));
  TEST_ASSERT_TRUE(hal_mock_tcp_queue_next_rx(
      reinterpret_cast<const uint8_t *>(reply), (uint16_t)strlen(reply)));

  char body[HAL_NOTIFY_TELEGRAM_TEXT_MAX + 501u] = {};
  memset(body, 'A', sizeof(body) - 1u);

  hal_notify_message_t message = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_message_init(&message));
  message.title = "Long report";
  message.body = body;

  hal_notify_receipt_t receipt = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_send(channel, &message, &receipt));
  TEST_ASSERT_EQUAL_UINT32(2u, receipt.parts_sent);
  TEST_ASSERT_EQUAL_UINT32(2u, receipt.parts_total);
  TEST_ASSERT_EQUAL_UINT32(0u,
                           receipt.flags & HAL_NOTIFY_RECEIPT_PARTIAL_DELIVERY);
  TEST_ASSERT_EQUAL_STRING("78", receipt.provider_message_id);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_close(channel));
}

void test_telegram_rate_limit_maps_retry_after(void) {
  hal_notify_telegram_config_t telegram = {};
  hal_notify_config_t config = {};
  init_local_telegram_config(&telegram, &config);

  hal_notify_channel_t channel = NULL;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_open(&config, &channel));

  const char body[] =
      "{\"ok\":false,\"error_code\":429,\"parameters\":{\"retry_after\":12}}";
  char reply[180] = {};
  snprintf(reply, sizeof(reply),
           "HTTP/1.1 429 Too Many Requests\r\nContent-Length: %zu\r\n\r\n%s",
           strlen(body), body);
  hal_mock_tcp_set_next_rx(reinterpret_cast<const uint8_t *>(reply),
                           (uint16_t)strlen(reply));

  hal_notify_message_t message = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_message_init(&message));
  message.body = "burst";

  hal_notify_receipt_t receipt = {};
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        hal_notify_send(channel, &message, &receipt));
  TEST_ASSERT_EQUAL_INT32(429, receipt.provider_status);
  TEST_ASSERT_EQUAL_INT32(429, receipt.provider_error);
  TEST_ASSERT_EQUAL_UINT32(12u, receipt.retry_after_s);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_notify_close(channel));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_config_message_and_backend_validation);
  RUN_TEST(test_fake_backend_send_poll_close_and_stale_handle);
  RUN_TEST(test_close_propagates_backend_failure_and_releases_channel);
  RUN_TEST(test_deferred_close_is_serialized_and_reported_by_active_send);
  RUN_TEST(test_channel_pool_reports_exhaustion_and_recovers_failed_open);
  RUN_TEST(test_telegram_requires_https_for_public_api_host);
  RUN_TEST(test_telegram_custom_http_host_uses_hal_http_client);
  RUN_TEST(test_telegram_splits_long_text_and_reports_all_parts);
  RUN_TEST(test_telegram_rate_limit_maps_retry_after);
  return UNITY_END();
}
