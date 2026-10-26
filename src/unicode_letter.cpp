#include "unicode_letter.h"

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace pele {
namespace {

#include "unicode_letter_data.inc"

// Pairs of start and end, unpacked from base-36 deltas.
const std::vector<unsigned>& table() {
  static const std::vector<unsigned> t = [] {
    std::vector<unsigned> out;
    const std::string packed(kPackedLetters);
    unsigned prev = 0;
    unsigned start = 0;
    bool is_start = true;
    std::size_t i = 0;
    while (i <= packed.size()) {
      const std::size_t sp = packed.find(' ', i);
      const std::string part = packed.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
      const unsigned value = static_cast<unsigned>(std::strtoul(part.c_str(), nullptr, 36));
      if (is_start) {
        start = prev + value;
        out.push_back(start);
      } else {
        prev = start + value;
        out.push_back(prev);
      }
      is_start = !is_start;
      if (sp == std::string::npos) break;
      i = sp + 1;
    }
    return out;
  }();
  return t;
}

}  // namespace

bool is_unicode_letter(int code) {
  if (code < 0xaa || code > 0xffdc) return false;
  const std::vector<unsigned>& t = table();
  std::ptrdiff_t lo = 0;
  std::ptrdiff_t hi = static_cast<std::ptrdiff_t>(t.size() >> 1) - 1;
  while (lo <= hi) {
    const std::ptrdiff_t mid = (lo + hi) >> 1;
    const unsigned c = static_cast<unsigned>(code);
    if (c < t[static_cast<std::size_t>(mid) * 2]) hi = mid - 1;
    else if (c > t[static_cast<std::size_t>(mid) * 2 + 1]) lo = mid + 1;
    else return true;
  }
  return false;
}

}  // namespace pele
