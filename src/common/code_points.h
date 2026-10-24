#pragma once

#include <vector>

#include "ustring.h"

namespace pele {

// `Array.from(string)`: the code points of a string, a surrogate pair as one and a lone surrogate by itself.
inline std::vector<ustring> code_points(ustring_view s) {
  std::vector<ustring> out;
  for (std::size_t i = 0; i < s.size(); i++) {
    const char16_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      out.push_back(ustring{c, s[i + 1]});
      i++;
    } else {
      out.push_back(ustring(1, c));
    }
  }
  return out;
}

// `chars.slice(from, to).join('')`.
inline ustring join_slice(const std::vector<ustring>& chars, std::size_t from, std::size_t to) {
  ustring out;
  for (std::size_t i = from; i < to && i < chars.size(); i++) out += chars[i];
  return out;
}

}  // namespace pele
