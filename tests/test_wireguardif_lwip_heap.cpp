// The real WireGuard engine (wireguardif.c, wireguard.c) on the real lwIP core
// built from third_party/lwip with tests/lwip_host: the device state must not
// live on the lwIP heap, and wireguardif_shutdown() must give back everything
// wireguardif_init() took from lwIP.

#include "lwip/init.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/netif.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "utils/unity.h"
#include "wireguard-platform.h"
#include "wireguardif.h"

#include <cstring>

namespace {

uint32_t s_now_ms = 1000u;
uint32_t s_random_state = 0x2545f491u;

// Any 32 bytes in base64; the engine clamps the scalar itself.
const char kPrivateKey[] = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=";
const char kBrokenKey[] = "not a key";

netif s_netif;
wireguardif_init_data s_init;

uint32_t heap_used() { return lwip_stats.mem.used; }
uint32_t heap_errors() { return lwip_stats.mem.err + lwip_stats.mem.illegal; }
uint32_t udp_pcbs_in_use() { return lwip_stats.memp[MEMP_UDP_PCB]->used; }
uint32_t timeouts_in_use() { return lwip_stats.memp[MEMP_SYS_TIMEOUT]->used; }

void prepare_init(const char *private_key) {
  memset(&s_netif, 0, sizeof(s_netif));
  memset(&s_init, 0, sizeof(s_init));
  s_init.private_key = private_key;
  s_init.listen_port = 51820u;
  s_init.bind_netif = nullptr;
  s_netif.state = &s_init;
}

} // namespace

extern "C" {

// NO_SYS lwIP and the engine's platform hooks, deterministic on the host.
u32_t sys_now(void) { return s_now_ms; }

uint32_t wireguard_sys_now(void) { return s_now_ms; }

void wireguard_random_bytes(void *bytes, size_t size) {
  uint8_t *out = static_cast<uint8_t *>(bytes);
  for (size_t index = 0u; index < size; ++index) {
    s_random_state = s_random_state * 1664525u + 1013904223u;
    out[index] = static_cast<uint8_t>(s_random_state >> 24u);
  }
}

void wireguard_tai64n_now(uint8_t *output) {
  // 2020-01-01 in TAI64N: seconds big endian after the 2^62 offset, no nanos.
  const uint64_t seconds = 0x4000000000000000ull + 1577836800ull + 37ull;
  for (int index = 0; index < 8; ++index) {
    output[index] = static_cast<uint8_t>(seconds >> (56 - 8 * index));
  }
  memset(output + 8, 0, 4u);
}

bool wireguard_is_under_load(void) { return false; }
}

void setUp(void) {}

void tearDown(void) {}

void test_init_keeps_the_device_off_the_lwip_heap_and_shutdown_returns_lwip_resources(
    void) {
  prepare_init(kPrivateKey);
  const uint32_t used_before = heap_used();
  const uint32_t timeouts_before = timeouts_in_use();

  TEST_ASSERT_EQUAL_INT(ERR_OK, wireguardif_init(&s_netif));

  TEST_ASSERT_NOT_NULL(s_netif.state);
  TEST_ASSERT_TRUE(s_netif.state != &s_init);
  TEST_ASSERT_EQUAL_CHAR('w', s_netif.name[0]);
  TEST_ASSERT_EQUAL_CHAR('g', s_netif.name[1]);
  TEST_ASSERT_EQUAL_UINT16(WIREGUARDIF_MTU, s_netif.mtu);
  // The device (peers, keypairs, replay windows) is not on the lwIP heap.
  TEST_ASSERT_EQUAL_UINT32(used_before, heap_used());
  // What the engine does take from lwIP: one UDP PCB and one periodic timer.
  TEST_ASSERT_EQUAL_UINT32(1u, udp_pcbs_in_use());
  TEST_ASSERT_EQUAL_UINT32(timeouts_before + 1u, timeouts_in_use());
  TEST_ASSERT_EQUAL_UINT32(0u, heap_errors());

  wireguardif_shutdown(&s_netif);

  TEST_ASSERT_NULL(s_netif.state);
  TEST_ASSERT_EQUAL_UINT32(used_before, heap_used());
  TEST_ASSERT_EQUAL_UINT32(0u, udp_pcbs_in_use());
  TEST_ASSERT_EQUAL_UINT32(timeouts_before, timeouts_in_use());
  TEST_ASSERT_EQUAL_UINT32(0u, heap_errors());
}

void test_init_with_a_broken_key_leaves_lwip_untouched(void) {
  prepare_init(kBrokenKey);
  const uint32_t used_before = heap_used();
  const uint32_t timeouts_before = timeouts_in_use();

  TEST_ASSERT_EQUAL_INT(ERR_ARG, wireguardif_init(&s_netif));

  TEST_ASSERT_NULL(s_netif.state);
  TEST_ASSERT_EQUAL_UINT32(used_before, heap_used());
  TEST_ASSERT_EQUAL_UINT32(0u, udp_pcbs_in_use());
  TEST_ASSERT_EQUAL_UINT32(timeouts_before, timeouts_in_use());
  TEST_ASSERT_EQUAL_UINT32(0u, heap_errors());
}

void test_repeated_init_and_shutdown_do_not_accumulate_lwip_resources(void) {
  const uint32_t used_before = heap_used();
  const uint32_t timeouts_before = timeouts_in_use();
  for (unsigned cycle = 0u; cycle < 8u; ++cycle) {
    prepare_init(kPrivateKey);
    TEST_ASSERT_EQUAL_INT(ERR_OK, wireguardif_init(&s_netif));
    TEST_ASSERT_EQUAL_UINT32(used_before, heap_used());
    wireguardif_shutdown(&s_netif);
    s_now_ms += 1000u;
  }
  TEST_ASSERT_EQUAL_UINT32(used_before, heap_used());
  TEST_ASSERT_EQUAL_UINT32(0u, udp_pcbs_in_use());
  TEST_ASSERT_EQUAL_UINT32(timeouts_before, timeouts_in_use());
  TEST_ASSERT_EQUAL_UINT32(0u, heap_errors());
}

int main(void) {
  lwip_init();
  UNITY_BEGIN();
  RUN_TEST(
      test_init_keeps_the_device_off_the_lwip_heap_and_shutdown_returns_lwip_resources);
  RUN_TEST(test_init_with_a_broken_key_leaves_lwip_untouched);
  RUN_TEST(test_repeated_init_and_shutdown_do_not_accumulate_lwip_resources);
  return UNITY_END();
}
