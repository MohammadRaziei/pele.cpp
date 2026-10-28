#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ustring.h"

namespace pele::theme {

// Private aliases set once on the root element, so each use is short and the public --pele-*
// tokens keep their fallbacks. The text colour is set as well: an icon is drawn in currentColor,
// which would otherwise be whatever colour the page gives the diagram's container.
extern const char* const kRootStyle;

inline constexpr const char* kFontMono = "var(--_fm)";
inline constexpr const char* kRadius = "var(--pele-radius,4px)";

// Use CSS for rx variables to support older browsers. The default goes first so diagram styles can
// override it.
inline constexpr const char* kRounded = " style=\"rx:var(--_r)\"";
std::string rounded(const std::string& attrs);
// The colour of what a diagram marks as critical. It is not one of the series colours, as a theme
// may put those in any order.
inline constexpr const char* kCritical = "var(--pele-critical,#e45756)";

// The fonts the labels were measured in are the ones they are drawn in, unless the page names
// others. The rest of what text inherits is pinned, as none of it was measured.
std::string font_style(double size, const std::optional<std::string>& family, const std::optional<std::string>& mono);

// What a Mermaid `style` or `classDef` declaration list means for a shape and for its text.
struct resolved_style {
  std::string shape;
  std::string line;
  std::string text;
  std::optional<std::string> stroke;
  bool bold = false;
  bool italic = false;
  std::optional<double> font_size;
};

// Splits Mermaid style declarations into what applies to a shape and what applies to its text.
resolved_style resolve_style(const std::vector<ustring>& declarations);

// A list of class names as ` a b c`, or an empty string.
std::string class_names(ustring_view raw);

// Colour for the nth data series in a chart, cycling through eight tokens.
std::string series_color(int index);

}  // namespace pele::theme
