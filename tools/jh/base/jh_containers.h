#ifndef JH_CONTAINERS_H
#define JH_CONTAINERS_H

/** @file jh_containers.h
 * C++ owners of the jhbase containers for the jh host commands: owned_string
 * holds a jb_string_t, owned_array a jb_vector_t of bytewise movable elements
 * and owned_list objects that need construction and destruction, through a
 * vector of pointers. Each owner binds init and destroy to its lifetime and
 * turns a failed jhbase operation into the exception the standard containers
 * throw: std::bad_alloc for HAL_ENOMEM, std::length_error for HAL_EOVERFLOW.
 * The operations themselves are those of jhbase, with jb_heap_allocator().
 * C++17.
 */
#include <cstddef>
#include <jb/string.h>
#include <jb/vector.h>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jh {

/** Throw what a standard container would for a failed jhbase container
 * operation: std::bad_alloc for HAL_ENOMEM, std::length_error otherwise.
 */
void check_container(hal_status_t status);

/** Owned bytes with a trailing NUL (jb_string_t). Copies are deep and may
 * throw, as std::string copies do; moves never allocate.
 */
class owned_string {
public:
  owned_string() noexcept;
  owned_string(const char *data, size_t length);
  /** From anything that converts to std::string_view, such as a literal, a
   * std::string or a view, so call sites keep their string expressions.
   */
  template <class S, std::enable_if_t<
                         std::is_convertible_v<const S &, std::string_view> &&
                             !std::is_same_v<std::decay_t<S>, owned_string>,
                         int> = 0>
  owned_string(const S &value)
      : owned_string(std::string_view(value).data(),
                     std::string_view(value).size()) {}
  owned_string(const owned_string &other);
  owned_string(owned_string &&other) noexcept;
  owned_string &operator=(const owned_string &other);
  owned_string &operator=(owned_string &&other) noexcept;
  ~owned_string();

  owned_string &assign(std::string_view value);
  owned_string &append(std::string_view value);
  owned_string &operator+=(std::string_view value) { return append(value); }
  owned_string &operator+=(char byte) {
    return append(std::string_view(&byte, 1));
  }
  /** Set the length to length, filling new bytes with fill. */
  void resize(size_t length, char fill = '\0');
  void clear() noexcept;

  /** NUL-terminated bytes, "" when empty; valid until the next change. */
  const char *c_str() const noexcept;
  const char *data() const noexcept { return c_str(); }
  /** Writable bytes of a nonempty string (jb_string_mutable_data). */
  char *mutable_data() noexcept;
  size_t size() const noexcept;
  bool empty() const noexcept { return size() == 0; }
  std::string_view view() const noexcept { return {c_str(), size()}; }
  operator std::string_view() const noexcept { return view(); }

  /** Byte comparison with another owner, a view, a std::string or a C string.
   */
  template <class S,
            std::enable_if_t<std::is_convertible_v<const S &, std::string_view>,
                             int> = 0>
  friend bool operator==(const owned_string &left, const S &right) noexcept {
    return left.view() == std::string_view(right);
  }
  template <class S, std::enable_if_t<
                         std::is_convertible_v<const S &, std::string_view> &&
                             !std::is_same_v<S, owned_string>,
                         int> = 0>
  friend bool operator==(const S &left, const owned_string &right) noexcept {
    return std::string_view(left) == right.view();
  }
  template <class S,
            std::enable_if_t<std::is_convertible_v<const S &, std::string_view>,
                             int> = 0>
  friend bool operator!=(const owned_string &left, const S &right) noexcept {
    return !(left == right);
  }
  template <class S, std::enable_if_t<
                         std::is_convertible_v<const S &, std::string_view> &&
                             !std::is_same_v<S, owned_string>,
                         int> = 0>
  friend bool operator!=(const S &left, const owned_string &right) noexcept {
    return !(right == left);
  }

private:
  jb_string_t value_{};
};

/** Owned contiguous elements (jb_vector_t). T is copied byte for byte, so it
 * must be trivially copyable; owned_list holds anything else. Move only.
 */
template <class T> class owned_array {
  static_assert(std::is_trivially_copyable_v<T>,
                "owned_array copies elements byte for byte");
  static_assert(alignof(T) <= alignof(jb_max_align_t),
                "owned_array needs the allocator's alignment");

public:
  owned_array() noexcept { reset(); }
  owned_array(const owned_array &) = delete;
  owned_array &operator=(const owned_array &) = delete;
  owned_array(owned_array &&other) noexcept : value_(other.value_) {
    other.reset();
  }
  owned_array &operator=(owned_array &&other) noexcept {
    if (this != &other) {
      jb_vector_destroy(&value_);
      value_ = other.value_;
      other.reset();
    }
    return *this;
  }
  ~owned_array() { jb_vector_destroy(&value_); }

  /** Resize to count elements; new elements hold zero bytes. */
  void resize(size_t count) {
    check_container(jb_vector_resize(&value_, count));
  }
  void reserve(size_t count) {
    check_container(jb_vector_reserve(&value_, count));
  }
  void push_back(const T &value) { append(&value, 1); }
  void append(const T *values, size_t count) {
    check_container(jb_vector_append(&value_, values, count));
  }
  void clear() noexcept { jb_vector_clear(&value_); }

  T *data() noexcept { return static_cast<T *>(jb_vector_data(&value_)); }
  const T *data() const noexcept {
    return static_cast<const T *>(jb_vector_const_data(&value_));
  }
  size_t size() const noexcept { return jb_vector_size(&value_); }
  bool empty() const noexcept { return size() == 0; }
  T &operator[](size_t index) noexcept { return data()[index]; }
  const T &operator[](size_t index) const noexcept { return data()[index]; }
  T &back() noexcept { return data()[size() - 1]; }
  T *begin() noexcept { return data(); }
  T *end() noexcept { return data() + size(); }
  const T *begin() const noexcept { return data(); }
  const T *end() const noexcept { return data() + size(); }

private:
  void reset() noexcept {
    value_ = {};
    // Initialization only checks its arguments, which are valid here.
    (void)jb_vector_init(&value_, sizeof(T), jb_heap_allocator());
  }
  jb_vector_t value_{};
};

/** Owned objects of any type, kept by pointer in an owned_array and destroyed
 * with the list. References stay valid until the element is removed. Move only.
 */
template <class T> class owned_list {
public:
  owned_list() noexcept = default;
  owned_list(const owned_list &) = delete;
  owned_list &operator=(const owned_list &) = delete;
  owned_list(owned_list &&) noexcept = default;
  owned_list &operator=(owned_list &&other) noexcept {
    if (this != &other) {
      clear();
      items_ = std::move(other.items_);
    }
    return *this;
  }
  ~owned_list() { clear(); }

  /** Take ownership of item and return it; on failure item is destroyed. */
  T &push_back(std::unique_ptr<T> item) {
    items_.push_back(item.get());
    return *item.release();
  }
  template <class... Args> T &emplace_back(Args &&...args) {
    return push_back(std::make_unique<T>(std::forward<Args>(args)...));
  }
  void clear() noexcept {
    for (T *item : items_)
      delete item;
    items_.clear();
  }

  size_t size() const noexcept { return items_.size(); }
  bool empty() const noexcept { return items_.empty(); }
  T &operator[](size_t index) noexcept { return *items_[index]; }
  const T &operator[](size_t index) const noexcept { return *items_[index]; }
  T &front() noexcept { return *items_[0]; }
  const T &front() const noexcept { return *items_[0]; }

  /** Forward iteration over the elements in insertion order. */
  template <class Item> class iterator {
  public:
    explicit iterator(T *const *at) noexcept : at_(at) {}
    Item &operator*() const noexcept { return **at_; }
    Item *operator->() const noexcept { return *at_; }
    iterator &operator++() noexcept {
      ++at_;
      return *this;
    }
    bool operator==(const iterator &other) const noexcept {
      return at_ == other.at_;
    }
    bool operator!=(const iterator &other) const noexcept {
      return at_ != other.at_;
    }

  private:
    T *const *at_;
  };
  iterator<T> begin() noexcept { return iterator<T>(items_.begin()); }
  iterator<T> end() noexcept { return iterator<T>(items_.end()); }
  iterator<const T> begin() const noexcept {
    return iterator<const T>(items_.begin());
  }
  iterator<const T> end() const noexcept {
    return iterator<const T>(items_.end());
  }

private:
  owned_array<T *> items_;
};

} // namespace jh
#endif
