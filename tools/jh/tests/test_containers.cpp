// The C++ owners of the jhbase containers (cxx/jh_containers.h): copies,
// moves and self-assignment, element ownership in owned_list, the exceptions
// for failed jhbase operations and copies of jh::failure (MSVC copies a thrown
// object byte for byte when it has no copy constructor). The C layer itself
// runs from c_layer_consumer.c, a C11 translation unit, with files in the
// directory of the argument. A native test, so the gate also runs it under
// Valgrind and the sanitizers.
#include "jh_adapters.h"
#include "jh_containers.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;
void expect(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "containers: %s\n", what);
    ++failures;
  }
}

template <class Call> bool throws_length_error(Call call) {
  try {
    call();
  } catch (const std::length_error &) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

void strings() {
  const jh::owned_string empty;
  expect(empty.empty() && empty.size() == 0 && empty.c_str()[0] == '\0',
         "an empty string has a terminated empty view");

  jh::owned_string text("ab\0c", 4);
  expect(text.size() == 4 && text.view() == std::string_view("ab\0c", 4) &&
             text.c_str()[4] == '\0',
         "embedded NUL bytes are kept, with a trailing NUL");

  const std::string standard = "standard";
  const jh::owned_string converted = standard;
  expect(converted == standard && standard == converted &&
             converted == "standard" && "standard" == converted &&
             converted != "other" && converted == std::string_view("standard"),
         "conversion from and comparison with standard strings and literals");

  jh::owned_string copy(text);
  copy.append("x");
  expect(text.size() == 4 && copy.size() == 5 && copy != text,
         "a copy owns its bytes");
  jh::owned_string assigned;
  assigned = copy;
  expect(assigned == copy, "copy assignment");
  jh::owned_string &alias = assigned;
  assigned = alias;
  expect(assigned == copy, "copy assignment to itself keeps the bytes");

  jh::owned_string moved(std::move(copy));
  expect(moved.size() == 5 && copy.empty(), "a move takes the bytes");
  copy += "reused";
  expect(copy == "reused", "a moved-from string stays usable");
  jh::owned_string target("old");
  target = std::move(moved);
  expect(target.size() == 5 && moved.empty(), "move assignment");
  jh::owned_string &self = target;
  target = std::move(self);
  expect(target.size() == 5, "move assignment to itself keeps the bytes");

  jh::owned_string sized;
  sized.resize(3, '-');
  sized.mutable_data()[1] = '+';
  expect(sized == "-+-", "resize fills and mutable_data writes in place");
  expect(throws_length_error([&] { sized.resize(SIZE_MAX); }) && sized == "-+-",
         "an impossible size throws length_error and keeps the bytes");
}

void arrays() {
  jh::owned_array<uint32_t> values;
  values.push_back(1);
  const uint32_t more[] = {2, 3};
  values.append(more, 2);
  values.resize(5);
  uint32_t sum = 0;
  for (uint32_t value : values)
    sum += value;
  expect(values.size() == 5 && values[3] == 0 && values[4] == 0 && sum == 6,
         "push_back, append and a zero-filling resize");

  jh::owned_array<uint32_t> moved(std::move(values));
  expect(moved.size() == 5 && values.empty(), "a move takes the elements");
  values.push_back(7);
  expect(values.size() == 1 && values[0] == 7,
         "a moved-from array stays usable");
  moved = std::move(values);
  expect(moved.size() == 1 && moved.back() == 7 && values.empty(),
         "move assignment releases the previous elements");
  jh::owned_array<uint32_t> &self = moved;
  moved = std::move(self);
  expect(moved.size() == 1, "move assignment to itself keeps the elements");
  expect(throws_length_error([&] { moved.resize(SIZE_MAX); }) &&
             moved.size() == 1,
         "an impossible size throws length_error and keeps the elements");
}

struct counted {
  static int live;
  int value;
  explicit counted(int number) : value(number) { ++live; }
  counted(const counted &) = delete;
  counted &operator=(const counted &) = delete;
  ~counted() { --live; }
};
int counted::live = 0;

void lists() {
  {
    jh::owned_list<counted> items;
    const counted &first = items.emplace_back(0);
    for (int number = 1; number < 100; ++number)
      items.emplace_back(number);
    expect(counted::live == 100 && &items[0] == &first &&
               items.front().value == 0,
           "elements keep their addresses while the list grows");
    int expected = 0;
    bool ordered = true;
    for (const counted &item : items)
      ordered = ordered && item.value == expected++;
    expect(ordered && expected == 100, "iteration in insertion order");

    counted &adopted = items.push_back(std::make_unique<counted>(500));
    expect(&adopted == &items[100] && counted::live == 101,
           "push_back takes ownership of the object");

    jh::owned_list<counted> moved(std::move(items));
    expect(moved.size() == 101 && items.empty() && counted::live == 101,
           "a move takes the elements without destroying them");
    jh::owned_list<counted> target;
    target.emplace_back(-1);
    target = std::move(moved);
    expect(target.size() == 101 && moved.empty() && counted::live == 101,
           "move assignment destroys the previous elements");
    target.clear();
    expect(target.empty() && counted::live == 0,
           "clear destroys every element");
    target.emplace_back(1);
  }
  expect(counted::live == 0, "the list destroys its elements");
}

void errors() {
  bool ok = true;
  try {
    jh::check_container(HAL_OK);
  } catch (...) {
    ok = false;
  }
  expect(ok, "HAL_OK throws nothing");
  bool memory = false;
  try {
    jh::check_container(HAL_ENOMEM);
  } catch (const std::bad_alloc &) {
    memory = true;
  }
  expect(memory, "HAL_ENOMEM throws bad_alloc");
  expect(throws_length_error([] { jh::check_container(HAL_EOVERFLOW); }),
         "HAL_EOVERFLOW throws length_error");

  const std::string long_message(1000, 'm');
  const jh::failure original{HAL_EIO,       3,         long_message, "JH-TEST",
                             "source.json", "/pointer"};
  jh::failure copy = original;
  copy.message.append("+");
  expect(original.message == long_message && copy.message.size() == 1001,
         "a copied failure owns its message");

  bool by_value = false;
  try {
    throw jh::failure{original};
  } catch (jh::failure caught) {
    by_value = caught.message == long_message && caught.code == "JH-TEST" &&
               caught.source == "source.json" && caught.pointer == "/pointer";
  }
  expect(by_value, "a failure caught by value keeps its fields");

  std::exception_ptr stored;
  try {
    throw jh::failure{original};
  } catch (...) {
    stored = std::current_exception();
  }
  bool rethrown = false;
  try {
    std::rethrow_exception(stored);
  } catch (const jh::failure &caught) {
    rethrown = caught.status == HAL_EIO && caught.message == long_message;
  }
  expect(rethrown, "a stored and rethrown failure keeps its fields");
}

} // namespace

extern "C" int jh_test_c_layer(const char *directory);

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: jh_containers_test <work directory>\n");
    return 2;
  }
  strings();
  arrays();
  lists();
  errors();
  failures += jh_test_c_layer(argv[1]);
  if (failures == 0)
    std::printf("containers: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
