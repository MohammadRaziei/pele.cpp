#include "ink.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <set>
#include <sstream>

#include "chars.h"

namespace pele {
namespace {

const std::set<std::string>& dark_names() {
  static const std::set<std::string> names = [] {
    std::set<std::string> out;
    std::istringstream in(
        "black blue blueviolet brown crimson darkblue darkcyan darkgreen darkmagenta darkolivegreen darkorchid darkred darkslateblue darkslategray darkviolet "
        "dimgray firebrick forestgreen green indigo maroon mediumblue mediumslateblue mediumvioletred midnightblue navy olive purple rebeccapurple royalblue "
        "saddlebrown seagreen sienna slateblue steelblue teal");
    for (std::string w; in >> w;) out.insert(w);
    return out;
  }();
  return names;
}

const std::set<std::string>& light_names() {
  static const std::set<std::string> names = [] {
    std::set<std::string> out;
    std::istringstream in(
        "aliceblue antiquewhite aqua aquamarine azure beige bisque blanchedalmond burlywood cadetblue chartreuse chocolate coral cornflowerblue cornsilk cyan "
        "darkgoldenrod darkgray darkkhaki darkorange darksalmon darkseagreen darkturquoise deeppink deepskyblue dodgerblue floralwhite fuchsia gainsboro "
        "ghostwhite gold goldenrod gray greenyellow honeydew hotpink indianred ivory khaki lavender lavenderblush lawngreen lemonchiffon lightblue lightcoral "
        "lightcyan lightgoldenrodyellow lightgray lightgreen lightpink lightsalmon lightseagreen lightskyblue lightslategray lightsteelblue lightyellow lime "
        "limegreen linen magenta mediumaquamarine mediumorchid mediumpurple mediumseagreen mediumspringgreen mediumturquoise mintcream mistyrose moccasin "
        "navajowhite oldlace olivedrab orange orangered orchid palegoldenrod palegreen paleturquoise palevioletred papayawhip peachpuff peru pink plum "
        "powderblue red rosybrown salmon sandybrown seashell silver skyblue slategray snow springgreen tan thistle tomato turquoise violet wheat white "
        "whitesmoke yellow yellowgreen");
    for (std::string w; in >> w;) out.insert(w);
    return out;
  }();
  return names;
}

const double kNaN = std::nan("");

double linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double luminance(double r, double g, double b) { return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b); }
double clamp01(double n) { return std::min(std::max(n, 0.0), 1.0); }

// Luminance where #1f1f1f and #fff have equal contrast.
double even() {
  static const double v = std::sqrt(1.05 * (luminance(31.0 / 255, 31.0 / 255, 31.0 / 255) + 0.05)) - 0.05;
  return v;
}

ustring lowered(ustring s) {
  for (char16_t& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char16_t>(c + 32);
  return s;
}

// /^[+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:e[+-]?\d+)?(%|deg|grad|rad|turn)?$/i: the number and its unit.
struct number_match {
  double value = 0;
  ustring unit;
};

std::optional<number_match> match_number(const ustring& s) {
  std::size_t i = 0;
  const std::size_t n = s.size();
  if (i < n && (s[i] == '+' || s[i] == '-')) i++;
  const std::size_t digits = i;
  while (i < n && is_digit(s[i])) i++;
  if (i > digits) {
    if (i + 1 < n && s[i] == '.' && is_digit(s[i + 1])) {
      i++;
      while (i < n && is_digit(s[i])) i++;
    }
  } else if (i + 1 < n && s[i] == '.' && is_digit(s[i + 1])) {
    i++;
    while (i < n && is_digit(s[i])) i++;
  } else {
    return std::nullopt;
  }
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    std::size_t k = i + 1;
    if (k < n && (s[k] == '+' || s[k] == '-')) k++;
    const std::size_t e_digits = k;
    while (k < n && is_digit(s[k])) k++;
    if (k > e_digits) i = k;
  }
  number_match m;
  m.value = std::strtod(to_utf8(s.substr(0, i)).c_str(), nullptr);
  m.unit = s.substr(i);
  const ustring unit = lowered(m.unit);
  if (!(unit.empty() || unit == u"%" || unit == u"deg" || unit == u"grad" || unit == u"rad" || unit == u"turn")) return std::nullopt;
  return m;
}

double amount(const ustring& raw, double full) {
  const auto m = match_number(raw);
  if (!m || (!m->unit.empty() && m->unit != u"%")) return kNaN;
  return clamp01(m->value / (!m->unit.empty() ? 100 : full));
}

double hue(const ustring& raw) {
  const auto m = match_number(raw);
  if (!m || m->unit == u"%") return kNaN;
  const ustring unit = lowered(m->unit);
  const double n = m->value;
  return unit == u"turn" ? n * 360 : unit == u"rad" ? (n * 180) / M_PI : unit == u"grad" ? n * 0.9 : n;
}

std::vector<ustring> split_on(const ustring& s, const std::function<std::size_t(const ustring&, std::size_t)>& sep_at, bool& any) {
  std::vector<ustring> out;
  std::size_t from = 0;
  std::size_t i = 0;
  any = false;
  while (i < s.size()) {
    const std::size_t len = sep_at(s, i);
    if (len > 0) {
      out.push_back(s.substr(from, i - from));
      i += len;
      from = i;
      any = true;
    } else {
      i++;
    }
  }
  out.push_back(s.substr(from));
  return out;
}

// \s*,\s*
std::size_t comma_sep(const ustring& s, std::size_t i) {
  std::size_t k = i;
  while (k < s.size() && is_space(s[k])) k++;
  if (k >= s.size() || s[k] != ',') return 0;
  k++;
  while (k < s.size() && is_space(s[k])) k++;
  return k - i;
}

// \s+
std::size_t space_sep(const ustring& s, std::size_t i) {
  std::size_t k = i;
  while (k < s.size() && is_space(s[k])) k++;
  return k - i;
}

// The comma syntax takes no `none`, rgb() channels all or none in %, and hsl() saturation and lightness in %.
std::optional<std::vector<ustring>> args(const ustring& body, bool hsl) {
  const bool legacy = body.find(u',') != ustring::npos;
  ustring main = body;
  std::optional<ustring> alpha;
  if (!legacy) {
    const std::size_t slash = body.find(u'/');
    if (slash != ustring::npos) {
      if (body.find(u'/', slash + 1) != ustring::npos) return std::nullopt;
      main = body.substr(0, slash);
      alpha = body.substr(slash + 1);
    }
  }
  bool any = false;
  std::vector<ustring> list = split_on(trim(main), legacy ? std::function<std::size_t(const ustring&, std::size_t)>(comma_sep) : space_sep, any);
  if (legacy ? (list.size() < 3 || list.size() > 4) : list.size() != 3) return std::nullopt;
  if (alpha) list.push_back(trim(*alpha));
  if (!legacy) {
    for (ustring& c : list)
      if (c == u"none") c = u"0";
    return list;
  }
  std::size_t percent = 0;
  for (std::size_t k = hsl ? 1 : 0; k < std::min<std::size_t>(3, list.size()); k++)
    if (!list[k].empty() && list[k].back() == '%') percent++;
  if (percent == (hsl ? 2u : 3u) || (!hsl && percent == 0)) return list;
  return std::nullopt;
}

bool is_hex_color(const ustring& v) {
  if (v.empty() || v[0] != '#') return false;
  const std::size_t n = v.size() - 1;
  if (!(n == 3 || n == 4 || n == 6 || n == 8)) return false;
  for (std::size_t i = 1; i < v.size(); i++)
    if (!is_hex(v[i])) return false;
  return true;
}

}  // namespace

std::optional<color_value> parse_color(const ustring& raw) {
  const ustring value = lowered(trim(raw));
  ustring name = value;
  const std::size_t grey = name.find(u"grey");
  if (grey != ustring::npos) name.replace(grey, 4, u"gray");
  const std::string name8 = to_utf8(name);
  if (dark_names().count(name8)) return color_value{0, 1};
  if (light_names().count(name8)) return color_value{1, 1};
  if (is_hex_color(value)) {
    ustring digits = value.substr(1);
    if (value.size() < 6) {
      ustring doubled;
      for (char16_t c : digits) {
        doubled += c;
        doubled += c;
      }
      digits = doubled;
    }
    auto n = [&](std::size_t i) { return static_cast<double>(std::stoi(to_utf8(digits.substr(2 * i, 2)), nullptr, 16)) / 255; };
    return color_value{luminance(n(0), n(1), n(2)), digits.size() == 8 ? n(3) : 1};
  }
  // /^(rgba?|hsla?)\(([^()]*)\)$/i
  const bool hsl = value.compare(0, 3, u"hsl") == 0;
  std::size_t open = std::string::npos;
  if (value.compare(0, 4, u"rgb(") == 0) open = 3;
  else if (value.compare(0, 5, u"rgba(") == 0) open = 4;
  else if (value.compare(0, 4, u"hsl(") == 0) open = 3;
  else if (value.compare(0, 5, u"hsla(") == 0) open = 4;
  if (open == std::string::npos || value.back() != ')') return std::nullopt;
  const ustring body = value.substr(open + 1, value.size() - open - 2);
  if (body.find_first_of(u"()") != ustring::npos) return std::nullopt;
  const auto list = args(body, hsl);
  if (!list) return std::nullopt;
  const double alpha = list->size() == 4 ? amount((*list)[3], 1) : 1;
  double rgb[3];
  if (!hsl) {
    for (int k = 0; k < 3; k++) rgb[k] = amount((*list)[k], 255);
  } else {
    double h = hue((*list)[0]);
    h = std::fmod(std::fmod(h, 360) + 360, 360) / 30;
    const double s = amount((*list)[1], 100);
    const double l = amount((*list)[2], 100);
    const double a = s * std::min(l, 1 - l);
    const int ks[3] = {0, 8, 4};
    for (int i = 0; i < 3; i++) {
      const double t = std::fmod(ks[i] + h, 12);
      rgb[i] = l - a * std::max(-1.0, std::min(std::min(t - 3, 9 - t), 1.0));
    }
  }
  const double lum = luminance(rgb[0], rgb[1], rgb[2]);
  if (std::isnan(lum + alpha)) return std::nullopt;
  return color_value{lum, alpha};
}

namespace {

// The value of the first `[";]fill:` in a style, up to the next `;` or `"`.
std::optional<std::string> fill_of(const std::string& style) {
  for (std::size_t i = 0; i + 6 <= style.size(); i++) {
    if ((style[i] == '"' || style[i] == ';') && style.compare(i + 1, 5, "fill:") == 0) {
      std::size_t end = i + 6;
      while (end < style.size() && style[end] != ';' && style[end] != '"') end++;
      return style.substr(i + 6, end - i - 6);
    }
  }
  return std::nullopt;
}

// How opaque a style's fill-opacity and opacity leave its fill, or NaN when either isn't a plain number.
double opacity(const std::string& shape) {
  double product = 1;
  for (std::size_t i = 0; i < shape.size(); i++) {
    if (shape[i] != '"' && shape[i] != ';') continue;
    std::size_t k = i + 1;
    if (shape.compare(k, 5, "fill-") == 0) k += 5;
    if (shape.compare(k, 8, "opacity:") != 0) continue;
    k += 8;
    std::size_t end = k;
    while (end < shape.size() && shape[end] != ';' && shape[end] != '"') end++;
    product *= amount(trim(to_utf16(shape.substr(k, end - k))), 1);
  }
  return product;
}

}  // namespace

std::optional<std::string> ink(const std::string& shape, const std::string& text) {
  const auto fill = fill_of(shape);
  if (!fill || fill_of(text)) return std::nullopt;
  const auto found = parse_color(to_utf16(*fill));
  if (!found) return std::nullopt;
  // An opacity that isn't a plain number, such as calc() or var(), may still be low.
  if (!(found->alpha * opacity(shape) >= 0.5)) return std::nullopt;
  return std::string(found->luminance > even() ? kOnLight : kOnDark);
}

bool paints(const std::string& shape) {
  const auto raw = fill_of(shape);
  if (!raw) return false;
  ustring fill = trim(to_utf16(*raw));
  fill = lowered(fill);
  if (fill.empty() || fill == u"none" || fill == u"transparent") return false;
  const double scale = opacity(shape);
  const auto color = parse_color(fill);
  return (color ? color->alpha : 1) * (std::isnan(scale) ? 1 : scale) > 0;
}

std::string ink_fill(const theme::resolved_style& style, const std::string& fallback) {
  const auto color = ink(style.shape, style.text);
  const std::string chosen = color ? *color : fallback;
  return chosen.empty() ? "" : " fill=\"" + chosen + "\"";
}

}  // namespace pele
