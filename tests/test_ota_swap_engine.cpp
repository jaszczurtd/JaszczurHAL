#include "hal/impl/shared/network/ota/jh_ota_swap_engine.h"
#include "utils/unity.h"

#include <initializer_list>
#include <string.h>

namespace {

constexpr uint32_t kSectors = 3u;

struct Fixture {
  uint32_t program[kSectors];
  uint32_t staging[kSectors];
  uint32_t scratch;
  uint8_t phase[kSectors];
  int mutation;
  int fail_at;
  bool fail_before;
};

uint32_t *slot(Fixture *fixture, jh_ota_swap_slot_t id, uint32_t sector) {
  switch (id) {
  case JH_OTA_SWAP_SLOT_PROGRAM:
    return &fixture->program[sector];
  case JH_OTA_SWAP_SLOT_STAGING:
    return &fixture->staging[sector];
  case JH_OTA_SWAP_SLOT_SCRATCH:
    return &fixture->scratch;
  default:
    return nullptr;
  }
}

hal_status_t read_phase(void *raw, uint32_t sector, uint8_t *out_phase) {
  auto *fixture = static_cast<Fixture *>(raw);
  if (sector >= kSectors || out_phase == nullptr) {
    return HAL_EINVAL;
  }
  *out_phase = fixture->phase[sector];
  return HAL_OK;
}

bool should_fail(Fixture *fixture, bool before) {
  fixture->mutation++;
  return fixture->fail_at == fixture->mutation &&
         fixture->fail_before == before;
}

hal_status_t copy_sector(void *raw, uint32_t sector,
                         jh_ota_swap_slot_t destination,
                         jh_ota_swap_slot_t source) {
  auto *fixture = static_cast<Fixture *>(raw);
  if (should_fail(fixture, true)) {
    return HAL_EIO;
  }
  uint32_t *destination_value = slot(fixture, destination, sector);
  uint32_t *source_value = slot(fixture, source, sector);
  if (destination_value == nullptr || source_value == nullptr) {
    return HAL_EINVAL;
  }
  *destination_value = *source_value;
  return should_fail(fixture, false) ? HAL_EIO : HAL_OK;
}

hal_status_t mark_phase(void *raw, uint32_t sector, uint8_t phase) {
  auto *fixture = static_cast<Fixture *>(raw);
  if (should_fail(fixture, true)) {
    return HAL_EIO;
  }
  fixture->phase[sector] = phase;
  return should_fail(fixture, false) ? HAL_EIO : HAL_OK;
}

const jh_ota_swap_backend_t kBackend = {read_phase, copy_sector, mark_phase};

Fixture initial_fixture(void) {
  Fixture fixture = {};
  for (uint32_t sector = 0u; sector < kSectors; ++sector) {
    fixture.program[sector] = 0x1000u + sector;
    fixture.staging[sector] = 0x2000u + sector;
    fixture.phase[sector] = 0xFFu;
  }
  return fixture;
}

void assert_swapped(const Fixture &fixture) {
  for (uint32_t sector = 0u; sector < kSectors; ++sector) {
    TEST_ASSERT_EQUAL_HEX32(0x2000u + sector, fixture.program[sector]);
    TEST_ASSERT_EQUAL_HEX32(0x1000u + sector, fixture.staging[sector]);
    TEST_ASSERT_EQUAL_HEX8(0xF8u, fixture.phase[sector]);
  }
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_swap_resumes_after_every_mutation_boundary(void) {
  for (bool fail_before : {false, true}) {
    for (int failure = 1; failure <= 36; ++failure) {
      Fixture fixture = initial_fixture();
      fixture.fail_at = failure;
      fixture.fail_before = fail_before;
      (void)jh_ota_swap_execute(&kBackend, &fixture, kSectors);
      fixture.fail_at = 0;
      fixture.mutation = 0;
      TEST_ASSERT_EQUAL_INT(HAL_OK,
                            jh_ota_swap_execute(&kBackend, &fixture, kSectors));
      assert_swapped(fixture);
    }
  }
}

void test_second_swap_rolls_back_to_original_program(void) {
  Fixture fixture = initial_fixture();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_swap_execute(&kBackend, &fixture, kSectors));
  memset(fixture.phase, 0xFF, sizeof(fixture.phase));
  fixture.mutation = 0;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_ota_swap_execute(&kBackend, &fixture, kSectors));
  for (uint32_t sector = 0u; sector < kSectors; ++sector) {
    TEST_ASSERT_EQUAL_HEX32(0x1000u + sector, fixture.program[sector]);
    TEST_ASSERT_EQUAL_HEX32(0x2000u + sector, fixture.staging[sector]);
  }
}

void test_swap_rejects_corrupt_phase(void) {
  Fixture fixture = initial_fixture();
  fixture.phase[0] = 0x00u;
  TEST_ASSERT_EQUAL_INT(HAL_EPROTO,
                        jh_ota_swap_execute(&kBackend, &fixture, kSectors));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_swap_resumes_after_every_mutation_boundary);
  RUN_TEST(test_second_swap_rolls_back_to_original_program);
  RUN_TEST(test_swap_rejects_corrupt_phase);
  return UNITY_END();
}
