#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "icons.h"
#include "text_measurer.h"
#include "yaml.h"

namespace pele {

// What a caller may set for a render. Everything is optional; the defaults are Pele's.
struct render_options {
  // How wide text is. The default is the table of Arial advances.
  const text_measurer* measurer = nullptr;
  // The fonts labels are measured in, and drawn in where the page sets no --pele-font or --pele-font-mono.
  std::optional<std::string> font_family;
  std::optional<std::string> font_family_mono;
  std::optional<double> font_size;
  // Parts of a diagram drawn in the monospace font: "members" (class), "attributes", "files", "messages".
  std::vector<std::string> monospace;
  std::optional<std::string> id_prefix;
  // Whether the SVG scales down with its container. Unset, it follows `useMaxWidth` in the diagram's config.
  std::optional<bool> responsive;
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
  // Settings that apply before the diagram's own front matter and directives.
  std::optional<yaml_value> config;
};

}  // namespace pele
