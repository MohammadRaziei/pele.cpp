#include "common/fit_text.h"

#include <algorithm>
#include <cmath>

#include "common/code_points.h"

namespace pele {

std::optional<fitted> fit_text(ustring_view text, double max, double size, double min, const text_measurer& measurer, int style) {
  if (text.empty() || max <= 0) return std::nullopt;
  double width = measurer.width(text, size, style);
  if (width <= max) return fitted{ustring(text), size, width};

  // Width grows with font size, so one division lands on the right size or just above it.
  double shrunk = std::max(min, std::floor((size * max) / width));
  while (shrunk >= min) {
    width = measurer.width(text, shrunk, style);
    if (width <= max) return fitted{ustring(text), shrunk, width};
    shrunk--;
  }

  const std::vector<ustring> chars = code_points(text);
  long low = 0;
  long high = static_cast<long>(chars.size()) - 1;
  while (low < high) {
    const long mid = (low + high + 1) >> 1;
    if (measurer.width(trim_end(join_slice(chars, 0, static_cast<std::size_t>(mid))) + u"\u2026", min, style) <= max) low = mid;
    else high = mid - 1;
  }
  if (low == 0) return std::nullopt;
  const ustring cut = trim_end(join_slice(chars, 0, static_cast<std::size_t>(low))) + u"\u2026";
  width = measurer.width(cut, min, style);
  if (width <= max) return fitted{cut, min, width};
  return std::nullopt;
}

}  // namespace pele
