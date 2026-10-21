#include "svg.h"

#include <array>
#include <cmath>

#include "entities.h"
#include "js_math.h"

namespace pele::svg {
namespace {

bool needs_escape(unsigned char c) { return c == '&' || c == '<' || c == '>' || c == '"' || c <= 8 || c == 0x0b || c == 0x0c || (c >= 0x0e && c <= 0x1f) || c == 0x7f; }

bool is_dropped(unsigned char c) { return c <= 8 || c == 0x0b || c == 0x0c || (c >= 0x0e && c <= 0x1f) || c == 0x7f; }

// The two decimals of a coordinate, by hundredths.
const std::array<std::string, 100>& fractions() {
  static const std::array<std::string, 100> table = [] {
    std::array<std::string, 100> t;
    for (int i = 0; i < 100; i++) t[i] = i == 0 ? "" : i % 10 == 0 ? "." + std::to_string(i / 10) : "." + std::string(i < 10 ? "0" : "") + std::to_string(i);
    return t;
  }();
  return table;
}

const char* kBold = " font-weight=\"var(--_w)\"";
const char* kItalic = " font-style=\"italic\"";

}  // namespace

std::string esc(std::string_view text) {
  bool any = false;
  for (char c : text)
    if (needs_escape(static_cast<unsigned char>(c))) {
      any = true;
      break;
    }
  if (!any) return std::string(text);
  std::string out;
  out.reserve(text.size() + 16);
  for (char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (!is_dropped(u)) out += c;
  }
  return out;
}

std::string esc(ustring_view text) { return esc(std::string_view(to_utf8(text))); }

std::string esc_text(ustring_view text) { return esc(ustring_view(decode_entities(ustring(text)))); }

std::string esc_text(std::string_view utf8) { return esc_text(ustring_view(to_utf16(utf8))); }

std::string num(double value) {
  const double n = js_round(value * 100);
  if (n == 0) return "0";
  if (n > 0) {
    if (n < 1e11) {
      const double q = std::floor(n / 100);
      return std::to_string(static_cast<long long>(q)) + fractions()[static_cast<int>(n - q * 100)];
    }
  } else if (n > -1e11) {
    const double m = -n;
    const double q = std::floor(m / 100);
    return "-" + std::to_string(static_cast<long long>(q)) + fractions()[static_cast<int>(m - q * 100)];
  }
  return js_number_string(n / 100);
}

std::string span_style(int style) {
  return std::string((style & text_style::bold) ? kBold : "") + ((style & text_style::italic) ? kItalic : "");
}

int shared_style(const label& l) {
  int shared = text_style::bold | text_style::italic;
  bool any = false;
  for (const auto& line : l.lines) {
    for (const span& s : line) {
      if (s.has_icon || s.text.empty()) continue;
      shared &= s.style;
      any = true;
    }
  }
  return any ? shared : 0;
}

std::string apply_shared_style(std::string attrs, int shared) {
  const bool has_weight = attrs.find(" font-weight=\"") != std::string::npos;
  const bool has_style = attrs.find(" font-style=\"") != std::string::npos;
  return attrs + ((shared & pele::text_style::bold) && !has_weight ? kBold : "") + ((shared & pele::text_style::italic) && !has_style ? kItalic : "");
}

std::string label_svg(const label& l, double cx, double cy, std::string attrs, const icon_set* icons) {
  const auto& lines = l.lines;
  if (lines.empty()) return "";
  const double lh = l.line_height;
  const double shift = l.size * 0.35;
  double y = cy - l.height / 2 + lh / 2 + shift;

  if (lines.size() == 1 && lines[0].size() == 1 && !lines[0][0].has_icon && lines[0][0].style == 0)
    return "<text" + attrs + " x=\"" + num(cx) + "\" y=\"" + num(y) + "\" text-anchor=\"middle\">" + esc(lines[0][0].text) + "</text>";

  const int shared = shared_style(l);
  attrs = apply_shared_style(attrs, shared);
  std::string body;
  std::string extra;
  for (std::size_t i = 0; i < lines.size(); i++) {
    const auto& line = lines[i];
    bool has_icon = false;
    for (const span& s : line)
      if (s.has_icon) has_icon = true;
    if (has_icon) {
      double x = cx - l.widths[i] / 2;
      for (const span& s : line) {
        if (s.has_icon) {
          const double side = l.size * 1.1;
          const std::string inner = icons != nullptr ? icons->markup(s.icon) : std::string();
          extra += "<svg class=\"pele-icon\" data-icon=\"" + esc_text(s.icon) + "\" x=\"" + num(x) + "\" y=\"" + num(y - shift - side / 2) + "\" width=\"" +
                   num(side) + "\" height=\"" + num(side) +
                   "\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" +
                   inner + "</svg>";
        } else if (!s.text.empty()) {
          body += "<tspan x=\"" + num(x) + "\" y=\"" + num(y) + "\" text-anchor=\"start\"" + span_style(s.style & ~shared) + ">" + esc(s.text) + "</tspan>";
        }
        x += s.width;
      }
    } else {
      for (std::size_t j = 0; j < line.size(); j++) {
        const span& s = line[j];
        const std::string pos = j == 0 ? " x=\"" + num(cx) + "\" y=\"" + num(y) + "\"" : "";
        body += "<tspan" + pos + span_style(s.style & ~shared) + ">" + esc(s.text) + "</tspan>";
      }
      if (line.empty()) body += "<tspan x=\"" + num(cx) + "\" y=\"" + num(y) + "\"> </tspan>";
    }
    y += lh;
  }
  return "<text" + attrs + " text-anchor=\"middle\" xml:space=\"preserve\">" + body + "</text>" + extra;
}

}  // namespace pele::svg
