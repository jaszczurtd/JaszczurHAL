#include "hal/bluetooth/jh_bluetooth_classic_hid_lifecycle.h"

#include <cstring>

#include "utils/unity.h"

namespace {

enum class Phase {
  kNone,
  kLinkKeyDb,
  kSdpClient,
  kHidHost,
  kEventHandler,
};

struct Fake {
  Phase fail_phase;
  char events[32];
  size_t event_count;
};

jh_bluetooth_classic_hid_lifecycle_t s_lifecycle;
Fake s_fake;

void record(char event) {
  TEST_ASSERT_LESS_THAN(sizeof(s_fake.events), s_fake.event_count);
  s_fake.events[s_fake.event_count++] = event;
}

hal_status_t start_phase(Phase phase, char event) {
  record(event);
  return s_fake.fail_phase == phase ? HAL_EIO : HAL_OK;
}

hal_status_t link_key_db_start(void *) {
  return start_phase(Phase::kLinkKeyDb, 'K');
}

void link_key_db_stop(void *) { record('k'); }

hal_status_t sdp_client_start(void *) {
  return start_phase(Phase::kSdpClient, 'S');
}

void sdp_client_stop(void *) { record('s'); }

hal_status_t hid_host_start(void *) {
  return start_phase(Phase::kHidHost, 'H');
}

void hid_host_stop(void *) { record('h'); }

hal_status_t event_handler_start(void *) {
  return start_phase(Phase::kEventHandler, 'E');
}

void event_handler_stop(void *) { record('e'); }

const jh_bluetooth_classic_hid_lifecycle_ops_t kOps = {
    .context = nullptr,
    .link_key_db_start = link_key_db_start,
    .link_key_db_stop = link_key_db_stop,
    .sdp_client_start = sdp_client_start,
    .sdp_client_stop = sdp_client_stop,
    .hid_host_start = hid_host_start,
    .hid_host_stop = hid_host_stop,
    .event_handler_start = event_handler_start,
    .event_handler_stop = event_handler_stop,
};

void assert_events(const char *expected) {
  TEST_ASSERT_EQUAL_STRING_LEN(expected, s_fake.events, s_fake.event_count);
  TEST_ASSERT_EQUAL_size_t(std::strlen(expected), s_fake.event_count);
}

void test_start_and_stop_use_fixed_order() {
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_classic_hid_lifecycle_start(&s_lifecycle, &kOps));
  assert_events("KSHE");

  jh_bluetooth_classic_hid_lifecycle_stop(&s_lifecycle, &kOps);
  assert_events("KSHEehsk");
  jh_bluetooth_classic_hid_lifecycle_stop(&s_lifecycle, &kOps);
  assert_events("KSHEehsk");
}

void test_each_failed_phase_rolls_back_completed_phases() {
  const struct {
    Phase phase;
    const char *events;
  } cases[] = {
      {Phase::kLinkKeyDb, "K"},
      {Phase::kSdpClient, "KSk"},
      {Phase::kHidHost, "KSHsk"},
      {Phase::kEventHandler, "KSHEhsk"},
  };

  for (const auto &test_case : cases) {
    std::memset(&s_lifecycle, 0, sizeof(s_lifecycle));
    std::memset(&s_fake, 0, sizeof(s_fake));
    s_fake.fail_phase = test_case.phase;
    TEST_ASSERT_EQUAL_INT(
        HAL_EIO, jh_bluetooth_classic_hid_lifecycle_start(&s_lifecycle, &kOps));
    assert_events(test_case.events);
  }
}

void test_invalid_and_repeated_start_are_rejected() {
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, jh_bluetooth_classic_hid_lifecycle_start(nullptr, &kOps));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_bluetooth_classic_hid_lifecycle_start(&s_lifecycle, &kOps));
  TEST_ASSERT_EQUAL_INT(
      HAL_EBUSY, jh_bluetooth_classic_hid_lifecycle_start(&s_lifecycle, &kOps));
}

} // namespace

void setUp() {
  std::memset(&s_lifecycle, 0, sizeof(s_lifecycle));
  std::memset(&s_fake, 0, sizeof(s_fake));
}

void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_and_stop_use_fixed_order);
  RUN_TEST(test_each_failed_phase_rolls_back_completed_phases);
  RUN_TEST(test_invalid_and_repeated_start_are_rejected);
  return UNITY_END();
}
