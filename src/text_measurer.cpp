#include "text_measurer.h"

#include <array>

namespace pele {
namespace {

#include "metrics_data.inc"

using table = std::array<std::uint16_t, 95>;

table unpack(const char* packed) {
  table out{};
  auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (int i = 0; i < 95; i++) out[i] = static_cast<std::uint16_t>(digit(packed[i * 2]) * 36 + digit(packed[i * 2 + 1]));
  return out;
}

const table& regular_table() {
  static const table t = unpack(kRegularMetrics);
  return t;
}

const table& bold_table() {
  static const table t = unpack(kBoldMetrics);
  return t;
}

bool is_wide(unsigned c) {
  return (c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) || (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff) ||
         (c >= 0xfe30 && c <= 0xfe4f) || (c >= 0xff00 && c <= 0xff60) || (c >= 0xffe0 && c <= 0xffe6) || (c >= 0xd800 && c <= 0xdbff);
}

bool is_zero_width(unsigned c) {
  return (c >= 0x300 && c <= 0x36f) || (c >= 0x200b && c <= 0x200f) || (c >= 0xdc00 && c <= 0xdfff) || (c >= 0xfe00 && c <= 0xfe0f) || c == 0xfeff;
}

}  // namespace

double metrics_measurer::width(ustring_view text, double size, int style) const {
  const bool mono = (style & text_style::mono) != 0;
  const table& t = (style & text_style::bold) ? bold_table() : regular_table();
  long units = 0;
  for (char16_t ch : text) {
    const unsigned c = ch;
    if (c < 32) continue;
    if (c < 127) units += mono ? 600 : t[c - 32];
    else if (is_zero_width(c)) continue;
    else if (is_wide(c)) units += mono ? 1200 : 1000;
    else units += mono ? 600 : 580;
  }
  return (static_cast<double>(units) * size) / 1000;
}

}  // namespace pele
