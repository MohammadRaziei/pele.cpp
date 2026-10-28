#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "chars.h"
#include "svg.h"

namespace pele::theme {

const char* const kRootStyle =
    "--_bg:var(--pele-bg,#fff);--_fg:var(--pele-fg,#1f1f1f);--_m:var(--pele-muted,#6e6e6e);"
    "--_l:var(--pele-line,#1f1f1f);--_s:var(--pele-surface,#f3f3f3);--_a:var(--pele-surface-alt,#e6e6e6);"
    "--_b:var(--pele-border,#8a8a8a);--_c:var(--pele-accent,#5b7bd5);color:var(--_fg)";

namespace {

#include "unicode_data.inc"

bool is_letter_or_number(unsigned cp) {
  std::size_t lo = 0;
  std::size_t hi = sizeof kLetterOrNumber / sizeof kLetterOrNumber[0];
  while (lo < hi) {
    const std::size_t mid = (lo + hi) / 2;
    if (cp < kLetterOrNumber[mid][0]) hi = mid;
    else if (cp > kLetterOrNumber[mid][1]) lo = mid + 1;
    else return true;
  }
  return false;
}

// The code point at i of a UTF-16 string, and how many units it takes.
unsigned code_point_at(ustring_view s, std::size_t i, std::size_t& len) {
  const unsigned c = s[i];
  if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
    len = 2;
    return 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
  }
  len = 1;
  return c;
}

// A quoted family: the quotes around anything but quotes, backslashes, line feeds and characters
// that could end the declaration it is put in.
bool quoted_family(ustring_view s, std::size_t& i) {
  const char16_t quote = s[i];
  std::size_t k = i + 1;
  while (k < s.size() && s[k] != quote) {
    const char16_t c = s[k];
    if (c == '"' || c == '\'' || c == '\\' || c == '\n' || c == '<' || c == '>' || c == '{' || c == '}' || c == '(' || c == ')' || c == ';' || c == ':' || c == '@')
      return false;
    k++;
  }
  if (k >= s.size()) return false;
  i = k + 1;
  return true;
}

bool unquoted_family(ustring_view s, std::size_t& i) {
  std::size_t len = 0;
  const unsigned first = code_point_at(s, i, len);
  if (!(is_letter_or_number(first) || first == '_' || first == '-')) return false;
  i += len;
  while (i < s.size()) {
    const unsigned c = code_point_at(s, i, len);
    if (!(is_letter_or_number(c) || c == '_' || c == ' ' || c == '.' || c == '-')) break;
    i += len;
  }
  return true;
}

// A list of font names, quoted or not, and nothing that could end the declaration it is put in.
bool is_family_list(ustring_view s) {
  std::size_t i = 0;
  auto skip_space = [&]() {
    while (i < s.size() && is_space(s[i])) i++;
  };
  skip_space();
  for (;;) {
    if (i >= s.size()) return false;
    const bool ok = (s[i] == '"' || s[i] == '\'') ? quoted_family(s, i) : unquoted_family(s, i);
    if (!ok) return false;
    skip_space();
    if (i >= s.size()) return true;
    if (s[i] != ',') return false;
    i++;
    skip_space();
  }
}

std::string families(const std::optional<std::string>& list, const char* generic) {
  if (list && !list->empty() && is_family_list(to_utf16(*list))) return *list;
  return generic;
}

const char* const kShapeProps[] = {"fill", "fill-opacity", "stroke", "stroke-width", "stroke-dasharray", "stroke-dashoffset", "stroke-linecap", "stroke-linejoin", "stroke-opacity", "opacity", "rx", "ry"};
const char* const kTextProps[] = {"color", "font-size", "font-family", "font-weight", "font-style", "text-decoration", "letter-spacing", "word-spacing", "opacity"};

template <std::size_t N>
bool in_list(const char* const (&list)[N], const std::string& name) {
  for (const char* p : list)
    if (name == p) return true;
  return false;
}

char lower(char16_t c) { return c < 128 && c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c < 128 ? c : '?'); }

bool starts_with_ci(ustring_view s, std::size_t at, const char* word) {
  for (std::size_t k = 0; word[k]; k++) {
    if (at + k >= s.size() || lower(s[at + k]) != word[k]) return false;
    if (s[at + k] >= 128) return false;
  }
  return true;
}

// Whether `word` followed by optional whitespace and `(` is at `at`.
bool call_at(ustring_view s, std::size_t at, const char* word) {
  std::size_t n = 0;
  while (word[n]) n++;
  if (!starts_with_ci(s, at, word)) return false;
  std::size_t k = at + n;
  while (k < s.size() && is_space(s[k])) k++;
  return k < s.size() && s[k] == '(';
}

// The patterns a value must not contain: url(, expression(, javascript:, any of <>{};\, @import, image-set and comments.
bool is_unsafe(ustring_view v) {
  for (std::size_t i = 0; i < v.size(); i++) {
    const char16_t c = v[i];
    if (c == '<' || c == '>' || c == '{' || c == '}' || c == ';' || c == '\\') return true;
    if (c == '/' && i + 1 < v.size() && v[i + 1] == '*') return true;
    if (call_at(v, i, "url") || call_at(v, i, "expression") || starts_with_ci(v, i, "javascript:") || starts_with_ci(v, i, "@import") || starts_with_ci(v, i, "image-set"))
      return true;
  }
  return false;
}

// Removes every `!important`, whatever its case.
ustring strip_important(ustring_view v) {
  ustring out;
  for (std::size_t i = 0; i < v.size();) {
    if (starts_with_ci(v, i, "!important")) i += 10;
    else out += v[i++];
  }
  return out;
}

// Number(text) for the values that can mean something here; NaN otherwise.
double js_number(ustring_view text) {
  const std::string t = to_utf8(trim(text));
  if (t.empty()) return 0;
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  return *end == '\0' ? v : std::nan("");
}

struct ordered_map {
  std::vector<std::pair<std::string, ustring>> items;
  void set(const std::string& k, ustring v) {
    for (auto& it : items)
      if (it.first == k) {
        it.second = std::move(v);
        return;
      }
    items.emplace_back(k, std::move(v));
  }
  const ustring* get(const std::string& k) const {
    for (const auto& it : items)
      if (it.first == k) return &it.second;
    return nullptr;
  }
};

}  // namespace

std::string font_style(double size, const std::optional<std::string>& family, const std::optional<std::string>& mono) {
  return ";--_fm:var(--pele-font-mono," + families(mono, "monospace") + ");font:" + svg::num(size) +
         "px sans-serif;letter-spacing:normal;word-spacing:normal;text-transform:none;direction:ltr;font-family:var(--pele-font," + families(family, "sans-serif") +
         ")";
}

resolved_style resolve_style(const std::vector<ustring>& declarations) {
  resolved_style none;
  if (declarations.empty()) return none;
  ordered_map shape;
  ordered_map text;
  for (const ustring& decl : declarations) {
    const std::size_t at = decl.find(u':');
    if (at == ustring::npos || at == 0) continue;
    std::string prop;
    for (char16_t c : trim(ustring_view(decl).substr(0, at))) prop += lower(c);
    const ustring value = trim(strip_important(ustring_view(decl).substr(at + 1)));
    if (value.empty() || is_unsafe(value) || svg::has_invalid_xml(value)) continue;
    if (in_list(kShapeProps, prop)) shape.set(prop, value);
    if (in_list(kTextProps, prop)) text.set(prop == "color" ? "fill" : prop, value);
  }
  if (shape.items.empty() && text.items.empty()) return none;
  std::string shape_css;
  std::string line_css;
  for (const auto& [prop, value] : shape.items) {
    shape_css += prop + ":" + to_utf8(value) + ";";
    if (prop != "fill" && prop != "fill-opacity") line_css += prop + ":" + to_utf8(value) + ";";
  }
  std::string text_css;
  for (const auto& [prop, value] : text.items) text_css += prop + ":" + to_utf8(value) + ";";
  const ustring* weight = text.get("font-weight");
  const ustring* size = text.get("font-size");
  std::optional<double> px;
  if (size != nullptr) {
    // /^[\d.]+(px)?$/
    ustring body = *size;
    if (body.size() >= 2 && body.substr(body.size() - 2) == u"px") body.resize(body.size() - 2);
    bool ok = !body.empty();
    for (char16_t c : body)
      if (!(is_digit(c) || c == '.')) ok = false;
    if (ok) px = std::strtod(to_utf8(body).c_str(), nullptr);
  }
  resolved_style r;
  r.shape = shape_css.empty() ? "" : " style=\"" + svg::esc(shape_css) + "\"";
  r.line = line_css.empty() ? "" : " style=\"" + svg::esc(line_css) + "\"";
  r.text = text_css.empty() ? "" : " style=\"" + svg::esc(text_css) + "\"";
  if (const ustring* s = shape.get("stroke")) r.stroke = to_utf8(*s);
  r.bold = weight != nullptr && (*weight == u"bold" || *weight == u"bolder" || js_number(*weight) >= 600);
  const ustring* font_style_value = text.get("font-style");
  r.italic = font_style_value != nullptr && *font_style_value == u"italic";
  if (px && *px > 0 && *px <= 200) r.font_size = *px;
  return r;
}

std::string class_names(ustring_view raw) {
  std::string cleaned;
  bool gap = false;
  for (char16_t c : raw) {
    if (is_word(c) || c == '-') {
      if (gap && !cleaned.empty()) cleaned += ' ';
      gap = false;
      cleaned += static_cast<char>(c);
    } else {
      gap = true;
    }
  }
  return cleaned.empty() ? "" : " " + cleaned;
}

std::string series_color(int index) {
  static const char* const kSeries[] = {"#4c78a8", "#f58518", "#54a24b", "#e45756", "#72b7b2", "#eeca3b", "#b279a2", "#9d755d"};
  const int k = ((index % 8) + 8) % 8;
  return "var(--pele-series-" + std::to_string(k + 1) + "," + kSeries[k] + ")";
}

std::string rounded(const std::string& attrs) {
  const std::size_t at = attrs.find(" style=\"");
  if (at == std::string::npos) return attrs + kRounded;
  return attrs.substr(0, at + 8) + "rx:var(--_r);" + attrs.substr(at + 8);
}

}  // namespace pele::theme
