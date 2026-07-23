#include "fakes/lwip_fake.h"
#include "hal/impl/shared/frameworks/wireguard/wireguard_pbuf.h"
#include "utils/unity.h"

#include <cstring>

void setUp(void) { lwip_fake_reset(); }

void tearDown(void) { TEST_ASSERT_EQUAL_size_t(0u, lwip_fake_pbuf_count()); }

void test_wireguard_pbuf_coalesces_all_segments_without_truncation(void) {
  pbuf *first = pbuf_alloc(PBUF_RAW, 3u, PBUF_RAM);
  pbuf *second = pbuf_alloc(PBUF_RAW, 5u, PBUF_RAM);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_INT(ERR_OK, pbuf_take(first, "abc", 3u));
  TEST_ASSERT_EQUAL_INT(ERR_OK, pbuf_take(second, "defgh", 5u));
  pbuf_cat(first, second);

  pbuf *contiguous = wireguard_pbuf_make_contiguous(first);
  TEST_ASSERT_NOT_NULL(contiguous);
  TEST_ASSERT_NULL(contiguous->next);
  TEST_ASSERT_EQUAL_UINT16(8u, contiguous->len);
  TEST_ASSERT_EQUAL_UINT16(8u, contiguous->tot_len);
  TEST_ASSERT_EQUAL_MEMORY("abcdefgh", contiguous->payload, 8u);
  pbuf_free(contiguous);
}

void test_wireguard_pbuf_preserves_already_contiguous_packet(void) {
  pbuf *packet = pbuf_alloc(PBUF_RAW, 4u, PBUF_RAM);
  TEST_ASSERT_NOT_NULL(packet);
  TEST_ASSERT_EQUAL_INT(ERR_OK, pbuf_take(packet, "data", 4u));

  TEST_ASSERT_EQUAL_PTR(packet, wireguard_pbuf_make_contiguous(packet));
  TEST_ASSERT_EQUAL_MEMORY("data", packet->payload, 4u);
  pbuf_free(packet);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_wireguard_pbuf_coalesces_all_segments_without_truncation);
  RUN_TEST(test_wireguard_pbuf_preserves_already_contiguous_packet);
  return UNITY_END();
}
