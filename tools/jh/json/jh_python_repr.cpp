#include "jh_python_repr.h"
#include <jb/unicode.h>
#include <jb/utf8.h>

namespace jh {
namespace {
/* A backslash, kind and digits hex digits of scalar: at most 10 bytes. */
struct escape {
  char bytes[10];
  size_t length;
  std::string_view view() const { return {bytes, length}; }
};

escape escaped(uint32_t scalar, char kind, unsigned digits) {
  constexpr char hex[] = "0123456789abcdef";
  escape out{{'\\', kind}, 2};
  for (unsigned shift = digits * 4; shift != 0; shift -= 4)
    out.bytes[out.length++] = hex[(scalar >> (shift - 4)) & 15];
  return out;
}

void append_piece(void *user, std::string_view piece, size_t) {
  static_cast<owned_string *>(user)->append(piece);
}
} // namespace

void python_repr(std::string_view text, repr_piece_fn piece, void *user) {
  const bool single = text.find('\'') != std::string_view::npos;
  const bool double_quote = text.find('"') != std::string_view::npos;
  const char quote = single && !double_quote ? '"' : '\'';
  const std::string_view quote_text(&quote, 1);
  piece(user, quote_text, 0);
  for (size_t i = 0; i < text.size();) {
    uint32_t scalar = 0;
    size_t used = 1;
    if (jb_utf8_decode(text.data() + i, text.size() - i, &scalar, &used) !=
        HAL_OK) {
      piece(user, escaped(static_cast<unsigned char>(text[i]), 'x', 2).view(),
            1);
      ++i;
      continue;
    }
    if (scalar == static_cast<unsigned char>(quote) || scalar == '\\') {
      const char pair[] = {'\\', static_cast<char>(scalar)};
      piece(user, std::string_view(pair, 2), used);
    } else if (scalar == '\t') {
      piece(user, "\\t", used);
    } else if (scalar == '\n') {
      piece(user, "\\n", used);
    } else if (scalar == '\r') {
      piece(user, "\\r", used);
    } else if (scalar < 0x20 || scalar == 0x7f) {
      piece(user, escaped(scalar, 'x', 2).view(), used);
    } else if (scalar < 0x7f) {
      piece(user, text.substr(i, used), used);
    } else {
      jb_unicode_properties_t properties;
      if (jb_unicode_properties(scalar, &properties) == HAL_OK &&
          properties.printable)
        piece(user, text.substr(i, used), used);
      else
        piece(user,
              (scalar < 0x100     ? escaped(scalar, 'x', 2)
               : scalar < 0x10000 ? escaped(scalar, 'u', 4)
                                  : escaped(scalar, 'U', 8))
                  .view(),
              used);
    }
    i += used;
  }
  piece(user, quote_text, 0);
}

owned_string python_repr(std::string_view text) {
  owned_string out;
  python_repr(text, append_piece, &out);
  return out;
}

} // namespace jh
