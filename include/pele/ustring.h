#pragma once

#include <string>
#include <string_view>

namespace pele {

// Text is kept as UTF-16, like the JavaScript strings the original works on: its widths, wrapping
// and scanning all count UTF-16 code units, and the same input has to give the same output.
using ustring = std::u16string;
using ustring_view = std::u16string_view;

// Invalid UTF-8 becomes U+FFFD.
ustring to_utf16(std::string_view utf8);
// A lone surrogate becomes U+FFFD.
std::string to_utf8(ustring_view utf16);

// A code point as UTF-16 (one unit, or two for one past U+FFFF).
ustring from_code_point(unsigned long cp);

// The same whitespace JavaScript's trim() removes.
ustring trim(ustring_view s);
ustring trim_start(ustring_view s);
ustring trim_end(ustring_view s);

// Index of the first occurrence at or after `from`, or npos.
inline std::size_t find_unit(ustring_view s, char16_t c, std::size_t from = 0) { return s.find(c, from); }

// "abc" literal to ustring, for ASCII text.
ustring ascii(std::string_view s);

}  // namespace pele
