#include "svg_document.h"

#include <set>

#include "js_math.h"
#include "svg.h"
#include "theme.h"

namespace pele {

std::string svg_document::build(std::string_view type, double width, double height, double font_size, const render_options& options, const accessible& acc,
                                std::string content) {
  const std::string prefix = svg::esc(options.id_prefix ? *options.id_prefix : std::string("pele"));
  std::string head;
  std::string aria;
  if (!acc.title.empty()) {
    head += "<title id=\"" + prefix + "-title\">" + svg::esc_text(acc.title) + "</title>";
    aria += " aria-labelledby=\"" + prefix + "-title\"";
  }
  if (!acc.description.empty()) {
    head += "<desc id=\"" + prefix + "-desc\">" + svg::esc_text(acc.description) + "</desc>";
    aria += " aria-describedby=\"" + prefix + "-desc\"";
  }
  const std::string fit = options.responsive.value_or(true) ? "max-width:100%;height:auto;" : "";
  // Which of the weights and the corner radius the content uses.
  std::set<std::string> used;
  for (std::size_t i = 0; i < content.size(); i++) {
    if (content.compare(i, 7, "var(--_") == 0) {
      std::size_t k = i + 7;
      std::string name;
      for (const char* w : {"r", "tw", "hw", "w"}) {
        const std::size_t n = std::char_traits<char>::length(w);
        if (content.compare(k, n, w) == 0 && k + n < content.size() && content[k + n] == ')') {
          name = w;
          break;
        }
      }
      if (!name.empty()) used.insert(name);
    }
  }
  std::string weights = used.count("r") ? std::string(";--_r:") + theme::kRadius : "";
  static const char* const kWeights[][2] = {{"tw", "title"}, {"hw", "heading"}, {"w", "bold"}};
  for (const auto& w : kWeights)
    if (used.count(w[0])) weights += std::string(";--_") + w[0] + ":var(--pele-" + w[1] + "-weight,bold)";
  const std::string size_w = js_number_string(width);
  const std::string size_h = js_number_string(height);
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"pele pele-" + std::string(type) + "\" viewBox=\"0 0 " + size_w + " " + size_h + "\" width=\"" + size_w +
         "\" height=\"" + size_h + "\" style=\"" + fit + theme::kRootStyle + weights + svg::esc(theme::font_style(font_size, options.font_family, options.font_family_mono)) +
         "\" fill=\"var(--_fg)\" role=\"graphics-document document\" aria-roledescription=\"" + std::string(type) + "\"" + aria + ">" + head + content + "</svg>";
}

}  // namespace pele
