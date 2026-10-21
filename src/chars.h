#pragma once

// Tests on UTF-16 code units, as the lexers read them with charCodeAt. Past the end of a string
// the code is -1, which is none of these.

namespace pele {

// Whitespace as JavaScript's `\s` sees it.
inline bool is_space(int c) {
  if (c < 0) return false;
  if (c < 128) return c == 32 || (c >= 9 && c <= 13);
  return c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 ||
         c == 0xfeff;
}

inline bool is_blank(int c) { return c == 32 || c == 9; }

// A line terminator: what `.` does not match.
inline bool is_line_end(int c) { return c == 10 || c == 13 || c == 0x2028 || c == 0x2029; }

inline bool is_digit(int c) { return c >= 48 && c <= 57; }

inline bool is_letter(int c) { return (c >= 65 && c <= 90) || (c >= 97 && c <= 122); }

inline bool is_hex(int c) { return (c >= 48 && c <= 57) || (c >= 65 && c <= 70) || (c >= 97 && c <= 102); }

// A character of `\w`.
inline bool is_word(int c) { return (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c == 95; }

}  // namespace pele
