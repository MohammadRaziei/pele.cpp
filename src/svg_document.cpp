#include "svg_document.h"

#include <set>

#include "chars.h"
#include "js_math.h"
#include "svg.h"
#include "theme.h"

namespace pele {
namespace {

// A rectangle's corner radius comes from a variable. Browsers only lately take a variable in the
// `rx` attribute, and all of them take one in a style, so that is where it is written. It goes
// first there, for a radius the diagram's author set to come after it and win.
const std::string kRounded = std::string(" rx=\"") + theme::kRadius + "\"";

bool is_word_char(char c) { return is_word(static_cast<unsigned char>(c)); }

std::string rounded(const std::string& content, std::set<std::string>& used) {
  std::string out;
  std::size_t from = 0;
  for (;;) {
    // `<rect\b[^>]*>`
    std::size_t at = content.find("<rect", from);
    while (at != std::string::npos && at + 5 < content.size() && is_word_char(content[at + 5])) at = content.find("<rect", at + 1);
    if (at == std::string::npos) break;
    const std::size_t close = content.find('>', at + 5);
    if (close == std::string::npos) break;
    out += content.substr(from, at - from);
    std::string tag = content.substr(at, close + 1 - at);
    from = close + 1;
    const std::size_t pos = tag.find(kRounded);
    if (pos == std::string::npos) {
      out += tag;
      continue;
    }
    used.insert("r");
    const std::string rest = tag.substr(0, pos) + tag.substr(pos + kRounded.size());
    const std::size_t style = rest.find(" style=\"");
    if (style != std::string::npos) {
      out += rest.substr(0, style + 8) + "rx:var(--_r);" + rest.substr(style + 8);
    } else {
      const bool self_closing = rest.size() >= 2 && rest.compare(rest.size() - 2, 2, "/>") == 0;
      const std::size_t end = self_closing ? rest.size() - 2 : rest.size() - 1;
      out += rest.substr(0, end) + " style=\"rx:var(--_r)\"" + rest.substr(end);
    }
  }
  out += content.substr(from);
  return out;
}

}  // namespace

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
  const std::string fit = options.responsive ? "max-width:100%;height:auto;" : "";
  // Which of the weights and the corner radius the content uses.
  std::set<std::string> used;
  for (std::size_t i = 0; i < content.size(); i++) {
    if (content.compare(i, kRounded.size(), kRounded) == 0) used.insert("rounded");
    else if (content.compare(i, 7, "var(--_") == 0) {
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
  if (used.count("rounded")) content = rounded(content, used);
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
