#include "hal/storage/jh_littlefs_provider.h"
#include "utils/unity.h"

#include <lfs.h>

#include <string.h>

namespace {

constexpr uint32_t kReadSize = 16u;
constexpr uint32_t kProgSize = 16u;
constexpr uint32_t kBlockSize = 128u;
constexpr uint32_t kBlockCount = 16u;
constexpr uint32_t kCacheSize = 64u;
constexpr uint32_t kLookaheadSize = 16u;
constexpr size_t kStorageSize = kBlockSize * kBlockCount;

struct fixture_t {
  uint8_t storage[kStorageSize];
  uint8_t read_buffer[kCacheSize];
  uint8_t prog_buffer[kCacheSize];
  uint8_t lookahead_buffer[kLookaheadSize];
  hal_status_t prepare_status;
  bool fail_read;
  bool fail_program;
  bool fail_erase;
  bool fail_sync;
  bool out_of_range;
  size_t read_calls;
  size_t program_calls;
  size_t erase_calls;
  size_t sync_calls;
  size_t progress_calls;
  void *progress_ctx;
};

fixture_t s_fixture = {};
jh_littlefs_geometry_t s_geometry = {};

hal_status_t prepare(void *context, jh_littlefs_geometry_t *out_geometry) {
  auto *fixture = static_cast<fixture_t *>(context);
  if (fixture->prepare_status != HAL_OK) {
    return fixture->prepare_status;
  }
  if (out_geometry == nullptr) {
    return HAL_EINVAL;
  }
  *out_geometry = s_geometry;
  return HAL_OK;
}

bool raw_range_valid(fixture_t *fixture, size_t offset, size_t size) {
  if (offset > sizeof(fixture->storage) ||
      size > sizeof(fixture->storage) - offset) {
    fixture->out_of_range = true;
    return false;
  }
  return true;
}

hal_status_t read(void *context, size_t offset, void *buffer, size_t size) {
  auto *fixture = static_cast<fixture_t *>(context);
  fixture->read_calls++;
  if (fixture->fail_read || buffer == nullptr ||
      !raw_range_valid(fixture, offset, size)) {
    return HAL_EIO;
  }
  memcpy(buffer, fixture->storage + offset, size);
  return HAL_OK;
}

hal_status_t program(void *context, size_t offset, const void *buffer,
                     size_t size, hal_littlefs_progress_callback_t progress,
                     void *progress_ctx) {
  auto *fixture = static_cast<fixture_t *>(context);
  fixture->program_calls++;
  if (fixture->fail_program || buffer == nullptr ||
      !raw_range_valid(fixture, offset, size)) {
    return HAL_EIO;
  }
  const auto *source = static_cast<const uint8_t *>(buffer);
  for (size_t i = 0u; i < size; ++i) {
    fixture->storage[offset + i] &= source[i];
  }
  if (progress != nullptr) {
    progress(progress_ctx);
  }
  return HAL_OK;
}

hal_status_t erase(void *context, size_t offset, size_t size,
                   hal_littlefs_progress_callback_t progress,
                   void *progress_ctx) {
  auto *fixture = static_cast<fixture_t *>(context);
  fixture->erase_calls++;
  if (fixture->fail_erase || !raw_range_valid(fixture, offset, size)) {
    return HAL_EIO;
  }
  memset(fixture->storage + offset, 0xff, size);
  if (progress != nullptr) {
    progress(progress_ctx);
  }
  return HAL_OK;
}

hal_status_t sync(void *context) {
  auto *fixture = static_cast<fixture_t *>(context);
  fixture->sync_calls++;
  return fixture->fail_sync ? HAL_EIO : HAL_OK;
}

const jh_littlefs_block_backend_t kBackend = {&s_fixture, prepare, read,
                                              program,    erase,   sync};

void progress_callback(void *context) {
  s_fixture.progress_calls++;
  s_fixture.progress_ctx = context;
}

int direct_read(const struct lfs_config *config, lfs_block_t block,
                lfs_off_t offset, void *buffer, lfs_size_t size) {
  (void)config;
  return jh_littlefs_block_read(&kBackend, &s_geometry, block, offset, buffer,
                                size) == HAL_OK
             ? LFS_ERR_OK
             : LFS_ERR_IO;
}

int direct_program(const struct lfs_config *config, lfs_block_t block,
                   lfs_off_t offset, const void *buffer, lfs_size_t size) {
  (void)config;
  return jh_littlefs_block_program(&kBackend, &s_geometry, block, offset,
                                   buffer, size, nullptr, nullptr) == HAL_OK
             ? LFS_ERR_OK
             : LFS_ERR_IO;
}

int direct_erase(const struct lfs_config *config, lfs_block_t block) {
  (void)config;
  return jh_littlefs_block_erase(&kBackend, &s_geometry, block, nullptr,
                                 nullptr) == HAL_OK
             ? LFS_ERR_OK
             : LFS_ERR_IO;
}

int direct_sync(const struct lfs_config *config) {
  (void)config;
  return jh_littlefs_block_sync(&kBackend) == HAL_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

bool create_test_file(void) {
  uint8_t read_buffer[kCacheSize] = {};
  uint8_t prog_buffer[kCacheSize] = {};
  uint8_t lookahead_buffer[kLookaheadSize] = {};
  struct lfs_config config = {};
  config.read = direct_read;
  config.prog = direct_program;
  config.erase = direct_erase;
  config.sync = direct_sync;
  config.read_size = kReadSize;
  config.prog_size = kProgSize;
  config.block_size = kBlockSize;
  config.block_count = kBlockCount;
  config.block_cycles = 100;
  config.cache_size = kCacheSize;
  config.lookahead_size = kLookaheadSize;
  config.read_buffer = read_buffer;
  config.prog_buffer = prog_buffer;
  config.lookahead_buffer = lookahead_buffer;

  lfs_t lfs = {};
  if (lfs_mount(&lfs, &config) != LFS_ERR_OK) {
    return false;
  }
  lfs_file_t file = {};
  const bool opened =
      lfs_file_open(&lfs, &file, "/present.txt",
                    LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == LFS_ERR_OK;
  const uint8_t payload[] = {1u, 2u, 3u, 4u};
  const bool written =
      opened && lfs_file_write(&lfs, &file, payload, sizeof(payload)) ==
                    (lfs_ssize_t)sizeof(payload);
  const bool closed = !opened || lfs_file_close(&lfs, &file) == LFS_ERR_OK;
  const bool unmounted = lfs_unmount(&lfs) == LFS_ERR_OK;
  return opened && written && closed && unmounted;
}

} // namespace

void setUp(void) {
  memset(&s_fixture, 0, sizeof(s_fixture));
  memset(s_fixture.storage, 0xff, sizeof(s_fixture.storage));
  s_fixture.prepare_status = HAL_OK;
  s_geometry = {kReadSize,
                kProgSize,
                kBlockSize,
                kBlockCount,
                100,
                kCacheSize,
                kLookaheadSize,
                s_fixture.read_buffer,
                s_fixture.prog_buffer,
                s_fixture.lookahead_buffer};
}

void tearDown(void) {}

void test_checked_block_adapters_reject_invalid_ranges_and_alignment(void) {
  uint8_t buffer[kProgSize] = {};
  const uint8_t programmed[kProgSize] = {};

  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_littlefs_block_read(&kBackend, &s_geometry,
                                                       kBlockCount - 1u,
                                                       kBlockSize - kReadSize,
                                                       buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_size_t(1u, s_fixture.read_calls);
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW, jh_littlefs_block_read(&kBackend, &s_geometry, kBlockCount,
                                            0u, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_littlefs_block_read(&kBackend, &s_geometry, 0u,
                                               kBlockSize + kReadSize, buffer,
                                               sizeof(buffer)));
  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW,
                        jh_littlefs_block_read(&kBackend, &s_geometry, 0u,
                                               kBlockSize - kReadSize, buffer,
                                               sizeof(buffer) + kReadSize));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_littlefs_block_read(&kBackend, &s_geometry, 0u, 1u,
                                               buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        jh_littlefs_block_read(&kBackend, &s_geometry, 0u, 0u,
                                               nullptr, sizeof(buffer)));
  TEST_ASSERT_EQUAL_size_t(1u, s_fixture.read_calls);

  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, jh_littlefs_block_program(&kBackend, &s_geometry, 0u, 1u,
                                            programmed, sizeof(programmed),
                                            progress_callback, &s_fixture));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, jh_littlefs_block_program(&kBackend, &s_geometry, 0u, 0u,
                                            programmed, sizeof(programmed) - 1u,
                                            progress_callback, &s_fixture));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        jh_littlefs_block_program(
                            &kBackend, &s_geometry, kBlockCount - 1u,
                            kBlockSize - kProgSize, programmed,
                            sizeof(programmed), progress_callback, &s_fixture));
  TEST_ASSERT_EQUAL_size_t(1u, s_fixture.program_calls);
  TEST_ASSERT_EQUAL_size_t(1u, s_fixture.progress_calls);

  TEST_ASSERT_EQUAL_INT(HAL_EOVERFLOW, jh_littlefs_block_erase(
                                           &kBackend, &s_geometry, kBlockCount,
                                           progress_callback, &s_fixture));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, jh_littlefs_block_erase(&kBackend, &s_geometry, kBlockCount - 1u,
                                      progress_callback, &s_fixture));
  TEST_ASSERT_EQUAL_size_t(1u, s_fixture.erase_calls);
  TEST_ASSERT_EQUAL_size_t(2u, s_fixture.progress_calls);
  TEST_ASSERT_FALSE(s_fixture.out_of_range);
  TEST_ASSERT_EQUAL_INT(HAL_OK, jh_littlefs_block_sync(&kBackend));
  TEST_ASSERT_EQUAL_size_t(1u, s_fixture.sync_calls);
}

void test_native_provider_rejects_invalid_geometry(void) {
  const jh_littlefs_provider_t *provider =
      jh_littlefs_lfs_provider_configure(&kBackend);
  TEST_ASSERT_NOT_NULL(provider);

  s_geometry.block_count = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, provider->ops->mount(provider->context));
  s_geometry.block_count = kBlockCount;
  s_geometry.block_size = 64u;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, provider->ops->mount(provider->context));
  s_geometry.block_size = kBlockSize;
  s_geometry.block_cycles = 0;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, provider->ops->mount(provider->context));
  TEST_ASSERT_EQUAL_size_t(0u, s_fixture.read_calls);
  TEST_ASSERT_EQUAL_size_t(0u, s_fixture.program_calls);
  TEST_ASSERT_EQUAL_size_t(0u, s_fixture.erase_calls);
}

void test_native_provider_runs_real_littlefs_lifecycle_and_errors(void) {
  const jh_littlefs_provider_t *provider =
      jh_littlefs_lfs_provider_configure(&kBackend);
  TEST_ASSERT_NOT_NULL(provider);
  TEST_ASSERT_NOT_NULL(provider->ops);

  s_fixture.prepare_status = HAL_ECONFIG;
  TEST_ASSERT_EQUAL_INT(HAL_ECONFIG, provider->ops->mount(provider->context));
  s_fixture.prepare_status = HAL_OK;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider->ops->mount(provider->context));

  uint32_t marker = 0x5544u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, provider->ops->set_progress_callback(provider->context,
                                                   progress_callback, &marker));
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->ops->format(provider->context));
  TEST_ASSERT_GREATER_THAN_size_t(0u, s_fixture.progress_calls);
  TEST_ASSERT_EQUAL_PTR(&marker, s_fixture.progress_ctx);
  TEST_ASSERT_FALSE(s_fixture.out_of_range);

  TEST_ASSERT_TRUE(create_test_file());
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->ops->mount(provider->context));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, provider->ops->exists(provider->context, "/present.txt"));
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, provider->ops->exists(provider->context, "/missing.txt"));

  size_t total = 0u;
  size_t used = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->ops->total_bytes(provider->context, &total));
  TEST_ASSERT_EQUAL_size_t(kStorageSize, total);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        provider->ops->used_bytes(provider->context, &used));
  TEST_ASSERT_GREATER_THAN_size_t(0u, used);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(total, used);

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, provider->ops->remove(provider->context, "/present.txt"));
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, provider->ops->remove(provider->context, "/present.txt"));
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->ops->unmount(provider->context));

  s_fixture.fail_read = true;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider->ops->mount(provider->context));
  s_fixture.fail_read = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->ops->mount(provider->context));
  TEST_ASSERT_EQUAL_INT(HAL_OK, provider->ops->unmount(provider->context));
  TEST_ASSERT_FALSE(s_fixture.out_of_range);
}

void test_native_provider_propagates_format_io_failures(void) {
  const jh_littlefs_provider_t *provider =
      jh_littlefs_lfs_provider_configure(&kBackend);
  TEST_ASSERT_NOT_NULL(provider);

  s_fixture.fail_erase = true;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider->ops->format(provider->context));
  TEST_ASSERT_GREATER_THAN_size_t(0u, s_fixture.erase_calls);

  memset(s_fixture.storage, 0xff, sizeof(s_fixture.storage));
  s_fixture.fail_erase = false;
  s_fixture.fail_program = true;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider->ops->format(provider->context));
  TEST_ASSERT_GREATER_THAN_size_t(0u, s_fixture.program_calls);

  memset(s_fixture.storage, 0xff, sizeof(s_fixture.storage));
  s_fixture.fail_program = false;
  s_fixture.fail_sync = true;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, provider->ops->format(provider->context));
  TEST_ASSERT_GREATER_THAN_size_t(0u, s_fixture.sync_calls);
  TEST_ASSERT_FALSE(s_fixture.out_of_range);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_checked_block_adapters_reject_invalid_ranges_and_alignment);
  RUN_TEST(test_native_provider_rejects_invalid_geometry);
  RUN_TEST(test_native_provider_runs_real_littlefs_lifecycle_and_errors);
  RUN_TEST(test_native_provider_propagates_format_io_failures);
  return UNITY_END();
}
