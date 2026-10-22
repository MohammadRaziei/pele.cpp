#pragma once

// Pele for C++: Mermaid diagrams as themeable SVG.
//
//   pele::render_result r = pele::render("flowchart TD\n  A --> B");
//   std::cout << r.svg;
//
// Errors are thrown as pele::pele_error.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "errors.h"
#include "icons.h"
#include "render_options.h"
#include "rendered.h"
#include "text_measurer.h"
#include "yaml.h"

namespace pele {

struct render_result {
  // The type as Pele names it ("flowchart") and as Mermaid reports it ("flowchart-v2").
  std::string type;
  std::string mermaid_type;
  std::string svg;
  double width = 0;
  double height = 0;
  std::vector<rendered_link> links;
};

// Draws diagram text (UTF-8) as an SVG document.
render_result render(std::string_view text, const render_options& options = {});

// The diagram type of the text, or none when it is not one Pele knows.
std::optional<std::string> detect_type(std::string_view text);

// Whether render() can draw the text's type.
bool supports(std::string_view text);

}  // namespace pele
