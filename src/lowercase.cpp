#include "lowercase.h"

#include <algorithm>
#include <iterator>

namespace pele {
namespace {

struct range {
  char32_t first;
  char32_t last;
};

// The code points String.prototype.toLowerCase changes, in order (tools/gen_lowercase_table.mjs).
const range kChanging[] = {
#include "lowercase_data.inc"
};

bool changes(char32_t cp) {
  const auto at = std::upper_bound(std::begin(kChanging), std::end(kChanging), cp, [](char32_t c, const range& r) { return c < r.first; });
  return at != std::begin(kChanging) && cp <= (at - 1)->last;
}

}  // namespace

bool changes_when_lowercased(ustring_view s) {
  for (std::size_t i = 0; i < s.size(); i++) {
    char32_t cp = s[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      i++;
    }
    if (changes(cp)) return true;
  }
  return false;
}

}  // namespace pele
