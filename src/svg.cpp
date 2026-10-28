#include "svg.h"

#include <array>
#include <cmath>

#include "entities.h"
#include "js_math.h"

namespace pele::svg {
namespace {

bool needs_escape(unsigned char c) { return c == 0xEF || c == '&' || c == '<' || c == '>' || c == '"' || c <= 8 || c == 0x0b || c == 0x0c || (c >= 0x0e && c <= 0x1f) || c == 0x7f; }

bool is_dropped(unsigned char c) { return c <= 8 || c == 0x0b || c == 0x0c || (c >= 0x0e && c <= 0x1f) || c == 0x7f; }

// U+FFFE and U+FFFF, which are not allowed in XML, as UTF-8.
bool is_noncharacter(std::string_view s, std::size_t i) {
  return static_cast<unsigned char>(s[i]) == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBF &&
         (static_cast<unsigned char>(s[i + 2]) == 0xBE || static_cast<unsigned char>(s[i + 2]) == 0xBF);
}

bool is_invalid_unit(char16_t c) { return c <= 8 || c == 0x0b || c == 0x0c || (c >= 0x0e && c <= 0x1f) || c == 0x7f || c == 0xfffe || c == 0xffff; }

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
const char* kUnderline = " text-decoration=\"underline\"";
const char* kMono = " font-family=\"var(--_fm)\"";
const char* kSub = " baseline-shift=\"sub\" font-size=\"smaller\"";
const char* kSup = " baseline-shift=\"super\" font-size=\"smaller\"";

}  // namespace

std::string esc(std::string_view text) {
  bool any = false;
  for (std::size_t i = 0; i < text.size(); i++) {
    const unsigned char u = static_cast<unsigned char>(text[i]);
    if (u == 0xEF ? is_noncharacter(text, i) : needs_escape(u)) {
      any = true;
      break;
    }
  }
  if (!any) return std::string(text);
  std::string out;
  out.reserve(text.size() + 16);
  for (std::size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    const unsigned char u = static_cast<unsigned char>(c);
    if (u == 0xEF && is_noncharacter(text, i)) {
      i += 2;
      continue;
    }
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (!is_dropped(u)) out += c;
  }
  return out;
}

bool has_invalid_xml(ustring_view text) {
  for (std::size_t i = 0; i < text.size(); i++) {
    const char16_t c = text[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
      i++;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      return true;
    } else if (is_invalid_unit(c)) {
      return true;
    }
  }
  return false;
}

// Drops the invalid XML characters, a lone surrogate among them, before the text is encoded.
std::string esc(ustring_view text) {
  ustring kept;
  const std::size_t n = text.size();
  for (std::size_t i = 0; i < n; i++) {
    const char16_t c = text[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
      kept += c;
      kept += text[++i];
    } else if (c < 0xD800 || c > 0xDFFF) {
      kept += c;
    }
  }
  return esc(std::string_view(to_utf8(kept)));
}

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
  return std::string((style & text_style::bold) ? kBold : "") + ((style & text_style::italic) ? kItalic : "") + ((style & text_style::mono) ? kMono : "") +
         ((style & text_style::underline) ? kUnderline : "") + ((style & text_style::sub) ? kSub : (style & text_style::sup) ? kSup : "");
}

int shared_style(const label& l) {
  int shared = text_style::bold | text_style::italic | text_style::mono;
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
  const bool has_family = attrs.find(" font-family=\"") != std::string::npos;
  return attrs + ((shared & pele::text_style::bold) && !has_weight ? kBold : "") + ((shared & pele::text_style::italic) && !has_style ? kItalic : "") +
         ((shared & pele::text_style::mono) && !has_family ? kMono : "");
}

std::string label_svg(const label& l, double cx, double cy, std::string attrs, const icon_set* icons) {
  const auto& lines = l.lines;
  if (lines.empty()) return "";
  const double lh = l.line_height;
  const double shift = l.size * 0.35;
  double top = cy - l.height / 2;

  if (lines.size() == 1 && lines[0].size() == 1 && !lines[0][0].has_icon && lines[0][0].style == 0)
    return "<text" + attrs + " x=\"" + num(cx) + "\" y=\"" + num(top + lh / 2 + shift) + "\" text-anchor=\"middle\">" + esc(lines[0][0].text) + "</text>";

  const int shared = shared_style(l);
  attrs = apply_shared_style(attrs, shared);
  std::string body;
  std::string extra;
  for (std::size_t i = 0; i < lines.size(); i++) {
    const auto& line = lines[i];
    const double h = l.heights.empty() ? lh : l.heights[i];
    const double mid = top + h / 2;
    const double y = mid + shift;
    bool has_icon = false;
    for (const span& s : line)
      if (s.has_icon) has_icon = true;
    if (has_icon) {
      double x = cx - l.widths[i] / 2;
      for (const span& s : line) {
        if (s.has_icon) {
          const double side = l.size * 1.1;
          const std::string inner = icons != nullptr ? icons->markup(s.icon) : std::string();
          extra += "<svg class=\"pele-icon\" data-icon=\"" + esc_text(s.icon) + "\" x=\"" + num(x) + "\" y=\"" + num(mid - side / 2) + "\" width=\"" +
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
    top += h;
  }
  return "<text" + attrs + " text-anchor=\"middle\" xml:space=\"preserve\">" + body + "</text>" + extra;
}

}  // namespace pele::svg
