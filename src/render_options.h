#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "icons.h"
#include "text_measurer.h"

namespace pele {

// What a caller may set for a render. Everything is optional; the defaults are Pele's.
struct render_options {
  // How wide text is. The default is the table of Arial advances.
  const text_measurer* measurer = nullptr;
  // The fonts labels are measured in, and drawn in where the page sets no --pele-font or --pele-font-mono.
  std::optional<std::string> font_family;
  std::optional<std::string> font_family_mono;
  std::optional<double> font_size;
  std::optional<std::string> id_prefix;
  bool responsive = true;
  // The width the host has for the diagram, in pixels. A type that can draw itself narrower does,
  // so that its text is not shrunk with it.
  std::optional<double> max_width;
  // Whether a diagram that runs across may be drawn running down when it does not fit `max_width`,
  // and the width under which that is done. On by default, under 640 pixels.
  bool auto_direction = true;
  std::optional<double> direction_breakpoint;
  std::optional<double> padding;
  std::optional<double> limit;
  std::optional<double> output_limit;
  // The most edges a flowchart may have. One short line can ask for millions of them.
  std::optional<double> max_edges;
  // Schemes a link or an image may use besides a relative address.
  std::optional<std::vector<std::string>> link_schemes;
  std::optional<std::vector<std::string>> image_schemes;
  bool links = true;
  bool images = true;
  // Link `rel` attribute. Defaults to `noopener`; an empty string omits it.
  std::optional<std::string> link_rel;
  // The host's icons.
  icon_set::resolver icons;
};

}  // namespace pele
