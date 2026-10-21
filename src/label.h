#pragma once

#include <string>
#include <vector>

#include "icons.h"
#include "text_measurer.h"
#include "ustring.h"

namespace pele {

// A run of text in one style, or an icon.
struct span {
  ustring text;
  int style = 0;
  bool has_icon = false;
  std::string icon;
  double width = 0;
};

// A label broken into lines of styled spans, and measured.
struct label {
  std::vector<std::vector<span>> lines;
  std::vector<double> widths;
  double width = 0;
  double height = 0;
  double size = 0;
  double line_height = 0;
};

inline constexpr double kLineHeight = 1.5;

// Breaks the text of a label into lines, styles and icons, and measures it. Mermaid's markup is
// read: <br>, <b>, <i>, entities, Font Awesome icons and, for markdown labels, emphasis.
class label_layouter {
 public:
  explicit label_layouter(const text_measurer& measurer, const icon_set* icons = nullptr) : measurer_(measurer), icons_(icons) {}

  // `max_width` is where a line wraps; `base` is the style the whole label starts in.
  label layout(ustring_view raw, bool markdown, double size, double max_width, int base = 0) const;

 private:
  void push_text(std::vector<span>& line, ustring_view text, int style) const;
  void push_icons(std::vector<span>& line, ustring_view text, int style) const;
  void inline_markup(std::vector<span>& line, ustring_view source, bool markdown, int base) const;
  double measure_spans(std::vector<span>& line, double size) const;
  void wrap(const std::vector<span>& line, double size, double max, std::vector<std::vector<span>>& out, std::vector<double>& widths) const;

  const text_measurer& measurer_;
  const icon_set* icons_;
};

}  // namespace pele
