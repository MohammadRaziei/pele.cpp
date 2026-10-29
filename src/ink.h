#pragma once

#include <optional>
#include <string>

#include "theme.h"

namespace pele {

inline constexpr const char* kOnLight = "var(--pele-fg-on-light,#1f1f1f)";
inline constexpr const char* kOnDark = "var(--pele-fg-on-dark,#fff)";

// The text color that reads best on the fill a style sets, as `shape` and `text` from
// theme::resolve_style(). Nothing where it sets no fill, the fill is not a color this knows,
// or it is below half opacity and so depends on an unknown background.
std::optional<std::string> ink(const std::string& shape, const std::string& text = "");

// Whether the style paints its shape. Unknown fill colors and opacities count as opaque.
bool paints(const std::string& shape);

// ` fill="..."` with the ink, or with the fallback when there is none, or nothing.
std::string ink_fill(const theme::resolved_style& style, const std::string& fallback = "");

// The luminance and alpha of a CSS color, for tests.
struct color_value {
  double luminance = 0;
  double alpha = 1;
};
std::optional<color_value> parse_color(const ustring& raw);

}  // namespace pele
