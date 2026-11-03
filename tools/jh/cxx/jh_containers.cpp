#include "jh_containers.h"
#include <new>
#include <stdexcept>

namespace jh {

void check_container(hal_status_t status) {
  if (status == HAL_OK)
    return;
  if (status == HAL_ENOMEM)
    throw std::bad_alloc();
  throw std::length_error("jhbase container operation failed");
}

owned_string::owned_string() noexcept {
  // Initialization only checks its arguments, which are valid here.
  (void)jb_string_init(&value_, jb_heap_allocator());
}

owned_string::owned_string(const char *data, size_t length) : owned_string() {
  check_container(jb_string_assign(&value_, data, length));
}

owned_string::owned_string(const owned_string &other) : owned_string() {
  check_container(jb_string_assign(&value_, other.data(), other.size()));
}

owned_string::owned_string(owned_string &&other) noexcept
    : value_(other.value_) {
  other.value_ = {};
  (void)jb_string_init(&other.value_, jb_heap_allocator());
}

owned_string &owned_string::operator=(const owned_string &other) {
  // assign accepts a source inside the same string, so self-assignment holds.
  return assign(other.view());
}

owned_string &owned_string::operator=(owned_string &&other) noexcept {
  if (this != &other) {
    jb_string_destroy(&value_);
    value_ = other.value_;
    other.value_ = {};
    (void)jb_string_init(&other.value_, jb_heap_allocator());
  }
  return *this;
}

owned_string::~owned_string() { jb_string_destroy(&value_); }

owned_string &owned_string::assign(std::string_view value) {
  check_container(jb_string_assign(&value_, value.data(), value.size()));
  return *this;
}

owned_string &owned_string::append(std::string_view value) {
  check_container(jb_string_append(&value_, value.data(), value.size()));
  return *this;
}

void owned_string::resize(size_t length, char fill) {
  check_container(jb_string_resize(&value_, length, fill));
}

void owned_string::clear() noexcept { jb_string_clear(&value_); }

const char *owned_string::c_str() const noexcept {
  const char *bytes = jb_string_data(&value_);
  return bytes != nullptr ? bytes : "";
}

char *owned_string::mutable_data() noexcept {
  return jb_string_mutable_data(&value_);
}

size_t owned_string::size() const noexcept { return jb_string_size(&value_); }

} // namespace jh
