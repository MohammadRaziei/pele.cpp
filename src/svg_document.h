#pragma once

#include <optional>
#include <string>

#include "render_options.h"
#include "ustring.h"

namespace pele {

// The accessible name and description a diagram may give itself.
struct accessible {
  ustring title;
  ustring description;
};

// The root element of a diagram: its size, the theme variables it sets once for everything inside,
// the accessible name, and the content.
class svg_document {
 public:
  static std::string build(std::string_view type, double width, double height, double font_size, const render_options& options, const accessible& acc,
                           std::string content);
};

}  // namespace pele
