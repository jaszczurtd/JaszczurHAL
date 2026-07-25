#include "hal/impl/shared/drivers/flash/jh_flash_transaction_engine.h"
#include "utils/unity.h"

#include <stddef.h>
#include <stdint.h>

namespace {

struct Fixture {
  char events[8];
  size_t event_count;
  hal_status_t acquire_status;
  hal_status_t quiesce_status;
  hal_status_t execute_status;
  hal_status_t resume_status;
  hal_status_t release_status;
  uint32_t seen_timeout_ms;
  void *seen_operation_context;
};

void record(Fixture *fixture, char event) {
  fixture->events[fixture->event_count++] = event;
}

hal_status_t acquire(void *context, uint32_t timeout_ms) {
  auto *fixture = static_cast<Fixture *>(context);
  record(fixture, 'A');
  fixture->seen_timeout_ms = timeout_ms;
  return fixture->acquire_status;
}

hal_status_t quiesce(void *context, uint32_t timeout_ms) {
  auto *fixture = static_cast<Fixture *>(context);
  record(fixture, 'Q');
  fixture->seen_timeout_ms = timeout_ms;
  return fixture->quiesce_status;
}

hal_status_t operation(void *context) {
  auto *fixture = static_cast<Fixture *>(context);
  record(fixture, 'O');
  return fixture->execute_status;
}

hal_status_t execute(void *context,
                     jh_flash_transaction_operation_t operation_callback,
                     void *operation_context, uint32_t timeout_ms) {
  auto *fixture = static_cast<Fixture *>(context);
  record(fixture, 'X');
  fixture->seen_timeout_ms = timeout_ms;
  fixture->seen_operation_context = operation_context;
  return operation_callback(operation_context);
}

hal_status_t resume(void *context) {
  auto *fixture = static_cast<Fixture *>(context);
  record(fixture, 'S');
  return fixture->resume_status;
}

hal_status_t release(void *context) {
  auto *fixture = static_cast<Fixture *>(context);
  record(fixture, 'R');
  return fixture->release_status;
}

const jh_flash_transaction_backend_t kBackend = {acquire, quiesce, execute,
                                                 resume, release};

Fixture make_fixture(void) {
  Fixture fixture = {};
  fixture.acquire_status = HAL_OK;
  fixture.quiesce_status = HAL_OK;
  fixture.execute_status = HAL_OK;
  fixture.resume_status = HAL_OK;
  fixture.release_status = HAL_OK;
  return fixture;
}

void assert_events(const Fixture &fixture, const char *expected,
                   size_t expected_count) {
  TEST_ASSERT_EQUAL_size_t(expected_count, fixture.event_count);
  TEST_ASSERT_EQUAL_CHAR_ARRAY(expected, fixture.events, expected_count);
}

} // namespace

void setUp(void) {}

void tearDown(void) {}

void test_flash_transaction_rejects_incomplete_contract(void) {
  Fixture fixture = make_fixture();
  jh_flash_transaction_backend_t incomplete = kBackend;
  incomplete.resume = nullptr;

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_flash_transaction_engine_execute(
                            nullptr, &fixture, operation, &fixture, 10u));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_flash_transaction_engine_execute(
                            &incomplete, &fixture, operation, &fixture, 10u));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, jh_flash_transaction_engine_execute(&kBackend, &fixture,
                                                      nullptr, &fixture, 10u));
  TEST_ASSERT_EQUAL_size_t(0u, fixture.event_count);
}

void test_flash_transaction_runs_all_phases_in_order(void) {
  Fixture fixture = make_fixture();

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_flash_transaction_engine_execute(&kBackend, &fixture,
                                                  operation, &fixture, 37u));
  assert_events(fixture, "AQXOSR", 6u);
  TEST_ASSERT_EQUAL_UINT32(37u, fixture.seen_timeout_ms);
  TEST_ASSERT_EQUAL_PTR(&fixture, fixture.seen_operation_context);
}

void test_flash_transaction_does_not_cleanup_unacquired_backend(void) {
  Fixture fixture = make_fixture();
  fixture.acquire_status = HAL_EBUSY;

  TEST_ASSERT_EQUAL_INT(
      HAL_EBUSY, jh_flash_transaction_engine_execute(&kBackend, &fixture,
                                                     operation, &fixture, 0u));
  assert_events(fixture, "A", 1u);
}

void test_flash_transaction_releases_after_quiesce_failure(void) {
  Fixture fixture = make_fixture();
  fixture.quiesce_status = HAL_ETIMEOUT;

  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        jh_flash_transaction_engine_execute(
                            &kBackend, &fixture, operation, &fixture, 5u));
  assert_events(fixture, "AQR", 3u);
}

void test_flash_transaction_preserves_operation_error_during_cleanup(void) {
  Fixture fixture = make_fixture();
  fixture.execute_status = HAL_EIO;
  fixture.resume_status = HAL_ETIMEOUT;
  fixture.release_status = HAL_EINTERNAL;

  TEST_ASSERT_EQUAL_INT(
      HAL_EIO, jh_flash_transaction_engine_execute(&kBackend, &fixture,
                                                   operation, &fixture, 5u));
  assert_events(fixture, "AQXOSR", 6u);
}

void test_flash_transaction_reports_cleanup_error_after_success(void) {
  Fixture fixture = make_fixture();
  fixture.resume_status = HAL_ETIMEOUT;
  fixture.release_status = HAL_EINTERNAL;

  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        jh_flash_transaction_engine_execute(
                            &kBackend, &fixture, operation, &fixture, 5u));
  assert_events(fixture, "AQXOSR", 6u);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_flash_transaction_rejects_incomplete_contract);
  RUN_TEST(test_flash_transaction_runs_all_phases_in_order);
  RUN_TEST(test_flash_transaction_does_not_cleanup_unacquired_backend);
  RUN_TEST(test_flash_transaction_releases_after_quiesce_failure);
  RUN_TEST(test_flash_transaction_preserves_operation_error_during_cleanup);
  RUN_TEST(test_flash_transaction_reports_cleanup_error_after_success);
  return UNITY_END();
}
