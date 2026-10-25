#pragma once

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

#include "ustring.h"

namespace pele {

// `parseFloat(text)`: the longest prefix that reads as a decimal number, after leading white space,
// or NaN. A hexadecimal prefix is not read, and `1_000` is `1`.
inline double js_parse_float(ustring_view s) {
  const auto white = [](char16_t c) {
    return c == 0x20 || (c >= 0x09 && c <= 0x0D) || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000 || c == 0xFEFF;
  };
  std::size_t i = 0;
  while (i < s.size() && white(s[i])) i++;
  std::string number;
  if (i < s.size() && (s[i] == u'+' || s[i] == u'-')) number += static_cast<char>(s[i++]);
  if (s.substr(i, 8) == u"Infinity") return number == "-" ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
  const auto digit = [&](std::size_t at) { return at < s.size() && s[at] >= u'0' && s[at] <= u'9'; };
  std::size_t whole = 0;
  while (digit(i)) {
    number += static_cast<char>(s[i++]);
    whole++;
  }
  std::size_t fraction = 0;
  if (i < s.size() && s[i] == u'.') {
    std::string tail = ".";
    std::size_t j = i + 1;
    while (digit(j)) {
      tail += static_cast<char>(s[j++]);
      fraction++;
    }
    if (whole > 0 || fraction > 0) {
      number += tail;
      i = j;
    }
  }
  if (whole == 0 && fraction == 0) return std::numeric_limits<double>::quiet_NaN();
  if (i < s.size() && (s[i] == u'e' || s[i] == u'E')) {
    std::size_t j = i + 1;
    std::string exponent = "e";
    if (j < s.size() && (s[j] == u'+' || s[j] == u'-')) exponent += static_cast<char>(s[j++]);
    if (digit(j)) {
      while (digit(j)) exponent += static_cast<char>(s[j++]);
      number += exponent;
    }
  }
  return std::strtod(number.c_str(), nullptr);
}

}  // namespace pele
