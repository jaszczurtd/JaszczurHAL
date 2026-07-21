#include "hal/impl/shared/network/jh_network_backend.h"
#include "hal/impl/shared/network/jh_network_handle_pool.h"
#include "utils/unity.h"

static hal_status_t service_init(void) { return HAL_OK; }
static hal_status_t service_step(void) { return HAL_OK; }

static jh_network_service_ops_t s_service = {
    service_init, nullptr, service_step, nullptr, nullptr,
};

static jh_network_backend_descriptor_t s_backend = {
    JH_NETWORK_BACKEND_ABI_VERSION,
    "test",
    JH_NET_CAP_WIFI_STA | JH_NET_CAP_IPV4,
    JH_NETWORK_EXECUTION_POLL,
    &s_service,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

void setUp(void) {}
void tearDown(void) {}

static void test_descriptor_rejects_invalid_abi(void) {
  jh_network_backend_descriptor_t backend = s_backend;
  backend.abi_version++;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, jh_network_backend_validate(&backend, 0u));
}

static void test_descriptor_checks_capability_and_operation_table(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, jh_network_backend_validate(
                                              &s_backend, JH_NET_CAP_UDP));
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, jh_network_backend_validate(
                                         &s_backend, JH_NET_CAP_WIFI_STA));
}

static void test_handle_generation_rejects_stale_ticket(void) {
  jh_network_handle_slot_t slots[2] = {};
  jh_network_handle_pool_t pool = {};
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_network_handle_pool_init(&pool, slots, 2u, 1u));

  int first_backend = 1;
  void *first_handle = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_allocate(&pool, &first_backend, &first_handle));
  void *resolved = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_handle_resolve(&pool, first_handle,
                                                          &resolved, nullptr));
  TEST_ASSERT_EQUAL_PTR(&first_backend, resolved);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_release(&pool, first_handle, &resolved));

  int second_backend = 2;
  void *second_handle = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_handle_allocate(
                                    &pool, &second_backend, &second_handle));
  TEST_ASSERT_NOT_EQUAL(first_handle, second_handle);
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_network_handle_resolve(&pool, first_handle, &resolved, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_handle_resolve(&pool, second_handle,
                                                          &resolved, nullptr));
  TEST_ASSERT_EQUAL_PTR(&second_backend, resolved);
}

static void test_handle_kind_rejects_cross_pool_ticket(void) {
  jh_network_handle_slot_t socket_slots[1] = {};
  jh_network_handle_slot_t listener_slots[1] = {};
  jh_network_handle_pool_t socket_pool = {};
  jh_network_handle_pool_t listener_pool = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_pool_init(&socket_pool, socket_slots, 1u, 1u));
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_network_handle_pool_init(
                                    &listener_pool, listener_slots, 1u, 2u));
  int backend = 1;
  void *handle = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_network_handle_allocate(&socket_pool, &backend, &handle));
  void *resolved = nullptr;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL,
      jh_network_handle_resolve(&listener_pool, handle, &resolved, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_descriptor_rejects_invalid_abi);
  RUN_TEST(test_descriptor_checks_capability_and_operation_table);
  RUN_TEST(test_handle_generation_rejects_stale_ticket);
  RUN_TEST(test_handle_kind_rejects_cross_pool_ticket);
  return UNITY_END();
}
