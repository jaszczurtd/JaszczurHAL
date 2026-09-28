#include "hal/network/ota/jh_ota_swap_engine.h"
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
  bool powered_off;
  bool keep_power;
  jh_ota_boot_state_t state;
  uint8_t state_index;
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

// Every write counts twice: before and after its effect. A failure cuts the
// power unless keep_power is set, so nothing after it reaches flash.
bool should_fail(Fixture *fixture, bool before) {
  if (fixture->powered_off) {
    return true;
  }
  fixture->mutation++;
  const bool fail =
      fixture->fail_at == fixture->mutation && fixture->fail_before == before;
  fixture->powered_off = fail && !fixture->keep_power;
  return fail;
}

void power_on(Fixture *fixture) {
  fixture->fail_at = 0;
  fixture->mutation = 0;
  fixture->powered_off = false;
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

hal_status_t write_state(void *raw, const jh_ota_boot_state_t *state,
                         uint8_t current_index) {
  auto *fixture = static_cast<Fixture *>(raw);
  if (should_fail(fixture, true)) {
    return HAL_EIO;
  }
  fixture->state = *state;
  fixture->state_index = (uint8_t)(current_index ^ 1u);
  return should_fail(fixture, false) ? HAL_EIO : HAL_OK;
}

hal_status_t erase_phase(void *raw) {
  auto *fixture = static_cast<Fixture *>(raw);
  if (should_fail(fixture, true)) {
    return HAL_EIO;
  }
  memset(fixture->phase, 0xFF, sizeof(fixture->phase));
  return should_fail(fixture, false) ? HAL_EIO : HAL_OK;
}

// The image tag stands in for SHA-256: sector n of image t holds t * 0x1000 +
// n.
bool slot_matches(void *raw, jh_ota_swap_slot_t id, uint32_t size,
                  const uint8_t sha256[JH_OTA_SHA256_BYTES]) {
  auto *fixture = static_cast<Fixture *>(raw);
  if (size != kSectors) {
    return false;
  }
  for (uint32_t sector = 0u; sector < kSectors; ++sector) {
    if (*slot(fixture, id, sector) != sha256[0] * 0x1000u + sector) {
      return false;
    }
  }
  return true;
}

const jh_ota_boot_backend_t kBootBackend = {
    {read_phase, copy_sector, mark_phase},
    write_state,
    erase_phase,
    slot_matches};

constexpr uint8_t kOldImage = 1u;
constexpr uint8_t kNewImage = 2u;
constexpr uint8_t kMaxAttempts = 3u;

Fixture initial_fixture(void) {
  Fixture fixture = {};
  for (uint32_t sector = 0u; sector < kSectors; ++sector) {
    fixture.program[sector] = 0x1000u + sector;
    fixture.staging[sector] = 0x2000u + sector;
    fixture.phase[sector] = 0xFFu;
  }
  return fixture;
}

Fixture boot_fixture(jh_ota_boot_mode_t mode, uint8_t attempts) {
  Fixture fixture = initial_fixture();
  fixture.state.sequence = 7u;
  fixture.state.mode = mode;
  fixture.state.attempts = attempts;
  fixture.state.max_attempts = kMaxAttempts;
  fixture.state.program_size = kSectors;
  fixture.state.staging_size = kSectors;
  fixture.state.program_sha256[0] = kOldImage;
  fixture.state.staging_sha256[0] = kNewImage;
  return fixture;
}

// One reset: the boot image reads the stored state and applies it.
jh_ota_boot_action_t boot(Fixture *fixture) {
  jh_ota_boot_state_t state = fixture->state;
  return jh_ota_boot_apply(&kBootBackend, fixture, &state, fixture->state_index,
                           kSectors);
}

// Resets after a power loss until the boot image launches the program.
void boot_until_launch(Fixture *fixture) {
  power_on(fixture);
  for (int reset = 0; reset < 4; ++reset) {
    if (boot(fixture) == JH_OTA_BOOT_ACTION_LAUNCH) {
      return;
    }
  }
  TEST_FAIL_MESSAGE("boot image never launched the program");
}

void assert_slot(const Fixture &fixture, jh_ota_swap_slot_t id, uint8_t tag) {
  Fixture copy = fixture;
  uint8_t digest[JH_OTA_SHA256_BYTES] = {tag};
  TEST_ASSERT_TRUE(slot_matches(&copy, id, kSectors, digest));
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
      power_on(&fixture);
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

void test_pending_update_survives_power_loss_at_every_step(void) {
  // 18 swap writes, the trial state and the journal erase.
  for (bool fail_before : {false, true}) {
    for (int failure = 1; failure <= 2 * 20 + 1; ++failure) {
      Fixture fixture = boot_fixture(JH_OTA_BOOT_PENDING, 0u);
      fixture.fail_at = failure;
      fixture.fail_before = fail_before;
      (void)boot(&fixture);
      boot_until_launch(&fixture);
      assert_slot(fixture, JH_OTA_SWAP_SLOT_PROGRAM, kNewImage);
      assert_slot(fixture, JH_OTA_SWAP_SLOT_STAGING, kOldImage);
      TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_TRIAL, fixture.state.mode);
      TEST_ASSERT_EQUAL_UINT8(kNewImage, fixture.state.program_sha256[0]);
      TEST_ASSERT_EQUAL_UINT8(kOldImage, fixture.state.staging_sha256[0]);
      TEST_ASSERT_TRUE(fixture.state.attempts <= 1u);
    }
  }
}

void test_rollback_survives_power_loss_at_every_step(void) {
  // Journal erase, rollback state, 18 swap writes, stable state, erase.
  for (bool fail_before : {false, true}) {
    for (int failure = 1; failure <= 2 * 22 + 1; ++failure) {
      Fixture fixture = boot_fixture(JH_OTA_BOOT_TRIAL, kMaxAttempts);
      for (uint32_t sector = 0u; sector < kSectors; ++sector) {
        fixture.program[sector] = 0x2000u + sector;
        fixture.staging[sector] = 0x1000u + sector;
      }
      fixture.state.program_sha256[0] = kNewImage;
      fixture.state.staging_sha256[0] = kOldImage;
      fixture.fail_at = failure;
      fixture.fail_before = fail_before;
      (void)boot(&fixture);
      boot_until_launch(&fixture);
      assert_slot(fixture, JH_OTA_SWAP_SLOT_PROGRAM, kOldImage);
      TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_STABLE, fixture.state.mode);
      TEST_ASSERT_EQUAL_UINT8(kOldImage, fixture.state.program_sha256[0]);
    }
  }
}

void test_corrupt_staging_keeps_program_and_enters_recovery(void) {
  Fixture fixture = boot_fixture(JH_OTA_BOOT_PENDING, 0u);
  fixture.staging[1] = 0xBADu;
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_ACTION_LAUNCH, boot(&fixture));
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_RECOVERY, fixture.state.mode);
  assert_slot(fixture, JH_OTA_SWAP_SLOT_PROGRAM, kOldImage);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, fixture.phase[0]);
}

void test_failed_swap_restarts_once_program_changed(void) {
  Fixture fixture = boot_fixture(JH_OTA_BOOT_PENDING, 0u);
  fixture.keep_power = true;
  fixture.fail_at = 6; // sector 0 of the program written, copy reports EIO
  fixture.fail_before = false;
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_ACTION_RESTART, boot(&fixture));
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_PENDING, fixture.state.mode);
}

void test_failed_swap_before_program_changed_launches_old_program(void) {
  Fixture fixture = boot_fixture(JH_OTA_BOOT_PENDING, 0u);
  fixture.keep_power = true;
  fixture.fail_at = 1; // first scratch copy
  fixture.fail_before = true;
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_ACTION_LAUNCH, boot(&fixture));
  assert_slot(fixture, JH_OTA_SWAP_SLOT_PROGRAM, kOldImage);
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_RECOVERY, fixture.state.mode);
}

void test_trial_boot_counts_attempts(void) {
  Fixture fixture = boot_fixture(JH_OTA_BOOT_TRIAL, 1u);
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_ACTION_LAUNCH, boot(&fixture));
  TEST_ASSERT_EQUAL_INT(JH_OTA_BOOT_TRIAL, fixture.state.mode);
  TEST_ASSERT_EQUAL_UINT8(2u, fixture.state.attempts);
  TEST_ASSERT_EQUAL_UINT32(8u, fixture.state.sequence);
  assert_slot(fixture, JH_OTA_SWAP_SLOT_PROGRAM, kOldImage);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_swap_resumes_after_every_mutation_boundary);
  RUN_TEST(test_second_swap_rolls_back_to_original_program);
  RUN_TEST(test_swap_rejects_corrupt_phase);
  RUN_TEST(test_pending_update_survives_power_loss_at_every_step);
  RUN_TEST(test_rollback_survives_power_loss_at_every_step);
  RUN_TEST(test_corrupt_staging_keeps_program_and_enters_recovery);
  RUN_TEST(test_failed_swap_restarts_once_program_changed);
  RUN_TEST(test_failed_swap_before_program_changed_launches_old_program);
  RUN_TEST(test_trial_boot_counts_attempts);
  return UNITY_END();
}
