#include "label.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "chars.h"
#include "entities.h"

namespace pele {
namespace {

// Rounds half up, as Math.round does.
double js_round(double v) { return std::floor(v + 0.5); }

// Break between any two Chinese or Japanese characters. Hangul is left out: Korean puts spaces between words.
bool is_cjk(int c) { return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xff60); }

bool in_set(int c, const char16_t* set) {
  for (; *set; set++)
    if (*set == c) return true;
  return false;
}

const char16_t* const kClose = u"、。，．：；！？）］｝」』】〕〉》〙〗ー…‥・ゝゞぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮ,.:;!?)]}";
const char16_t* const kOpen = u"（［｛「『【〔〈《〘〖([{";

bool is_close(int c) { return in_set(c, kClose); }
bool is_open(int c) { return in_set(c, kOpen); }

bool contains(ustring_view s, ustring_view part) { return s.find(part) != ustring_view::npos; }

bool contains_cjk(ustring_view s) {
  for (char16_t c : s)
    if (is_cjk(c)) return true;
  return false;
}

// Whether `fa[bklrs]?:fa-` starts at i; returns the length of the prefix, or 0.
std::size_t icon_prefix(ustring_view s, std::size_t i) {
  if (i + 1 >= s.size() || s[i] != 'f' || s[i + 1] != 'a') return 0;
  std::size_t k = i + 2;
  if (k < s.size() && (s[k] == 'b' || s[k] == 'k' || s[k] == 'l' || s[k] == 'r' || s[k] == 's')) k++;
  if (s.substr(k, 4) != u":fa-") return 0;
  return k + 4 - i;
}

// Whether the label has anything that needs reading as markup.
bool has_markup(ustring_view s) {
  for (std::size_t i = 0; i < s.size(); i++) {
    const char16_t c = s[i];
    if (c == '<' || c == '\\' || c == '\n' || c == '*' || c == '_') return true;
    if (c == 'f' && icon_prefix(s, i) > 0) return true;
  }
  return false;
}

ustring dedent(const ustring& text) {
  std::vector<ustring> lines;
  std::size_t from = 0;
  for (;;) {
    const std::size_t at = text.find(u'\n', from);
    if (at == ustring::npos) {
      lines.push_back(text.substr(from));
      break;
    }
    lines.push_back(text.substr(from, at - from));
    from = at + 1;
  }
  std::size_t min = std::numeric_limits<std::size_t>::max();
  for (const ustring& line : lines) {
    if (trim(line).empty()) continue;
    const std::size_t indent = line.size() - trim_start(line).size();
    if (indent < min) min = indent;
  }
  if (min == std::numeric_limits<std::size_t>::max() || min == 0) return text;
  ustring out;
  for (std::size_t i = 0; i < lines.size(); i++) {
    if (i > 0) out += u'\n';
    const ustring& line = lines[i];
    out += line.substr(std::min(min, line.size() - trim_start(line).size()));
  }
  return out;
}

// The end of an HTML tag at i, like `<\/?([a-zA-Z][\w-]*)(?:\s[^<>]*)?\/?>`, or npos; `name` gets its name.
std::size_t match_tag(ustring_view s, std::size_t i, ustring& name) {
  const std::size_t n = s.size();
  std::size_t k = i + 1;
  if (k < n && s[k] == '/') k++;
  if (k >= n || !is_letter(s[k])) return ustring::npos;
  const std::size_t name_at = k;
  while (k < n && (is_word(s[k]) || s[k] == '-')) k++;
  name = ustring(s.substr(name_at, k - name_at));
  if (k < n && is_space(s[k])) {
    while (k < n && s[k] != '<' && s[k] != '>') k++;
    if (k < n && s[k] == '>') return k + 1;
    return ustring::npos;
  }
  if (k < n && s[k] == '/') k++;
  if (k < n && s[k] == '>') return k + 1;
  return ustring::npos;
}

bool is_br_at(ustring_view s, std::size_t i, std::size_t& len) {
  const std::size_t n = s.size();
  if (i + 2 >= n || s[i] != '<' || (s[i + 1] | 0x20) != 'b' || (s[i + 2] | 0x20) != 'r') return false;
  std::size_t k = i + 3;
  while (k < n && is_space(s[k])) k++;
  if (k < n && s[k] == '/') k++;
  if (k < n && s[k] == '>') {
    len = k + 1 - i;
    return true;
  }
  return false;
}

// Splits at <br>, a literal backslash-n and newlines.
std::vector<ustring> split_breaks(const ustring& source) {
  std::vector<ustring> parts;
  ustring current;
  for (std::size_t i = 0; i < source.size();) {
    std::size_t len = 0;
    if (is_br_at(source, i, len)) {
      parts.push_back(current);
      current.clear();
      i += len;
    } else if (source[i] == '\\' && i + 1 < source.size() && (source[i + 1] | 0x20) == 'n') {
      parts.push_back(current);
      current.clear();
      i += 2;
    } else if (source[i] == '\n') {
      parts.push_back(current);
      current.clear();
      i++;
    } else {
      current += source[i++];
    }
  }
  parts.push_back(current);
  return parts;
}

// The pieces a line may break between: `\s*(?:[OPEN]*[CJK][CLOSE]*|[^\sCJK]+[CLOSE]*)\s*|\s+`.
std::vector<ustring> split_pieces(const ustring& s) {
  std::vector<ustring> pieces;
  const std::size_t n = s.size();
  std::size_t p = 0;
  while (p < n) {
    std::size_t q = p;
    while (q < n && is_space(s[q])) q++;
    std::size_t end = ustring::npos;
    // [OPEN]* then one CJK character: take as many openers as leave a CJK character after them.
    std::size_t run = 0;
    while (q + run < n && is_open(s[q + run])) run++;
    for (std::size_t j = run + 1; j-- > 0;) {
      if (q + j < n && is_cjk(s[q + j])) {
        end = q + j + 1;
        break;
      }
    }
    if (end == ustring::npos) {
      std::size_t k = q;
      while (k < n && !is_space(s[k]) && !is_cjk(s[k])) k++;
      if (k > q) end = k;
    }
    if (end != ustring::npos) {
      while (end < n && is_close(s[end])) end++;
      while (end < n && is_space(s[end])) end++;
    } else if (q > p) {
      end = q;  // whitespace only
    } else {
      break;
    }
    pieces.push_back(s.substr(p, end - p));
    p = end;
  }
  return pieces;
}

}  // namespace

void label_layouter::push_text(std::vector<span>& line, ustring_view text, int style) const {
  if (text.empty()) return;
  ustring decoded = decode_entities(ustring(text));
  if (!line.empty() && line.back().style == style && !line.back().has_icon) {
    line.back().text += decoded;
  } else {
    span s;
    s.text = std::move(decoded);
    s.style = style;
    line.push_back(std::move(s));
  }
}

void label_layouter::push_icons(std::vector<span>& line, ustring_view text, int style) const {
  if (!contains(text, u":fa-")) {
    push_text(line, text, style);
    return;
  }
  std::size_t last = 0;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t prefix = text[at] == 'f' ? icon_prefix(text, at) : 0;
    std::size_t k = at + prefix;
    while (prefix > 0 && k < text.size() && (is_word(text[k]) || text[k] == '-')) k++;
    if (prefix == 0 || k == at + prefix) {
      at++;
      continue;
    }
    // text[at, k) is `fa[bklrs]?:fa-name`.
    const std::size_t colon = text.find(u':', at);
    const std::string icon = to_utf8(text.substr(at, colon - at)) + ":fa-" + to_utf8(text.substr(at + prefix, k - at - prefix));
    push_text(line, text.substr(last, at - last), style);
    last = k;
    const bool known = icons_ != nullptr && !icons_->markup(icon).empty();
    if (known) {
      span s;
      s.style = style;
      s.has_icon = true;
      s.icon = icon;
      line.push_back(std::move(s));
    } else if (last < text.size() && text[last] == ' ') {
      // An icon the host does not have leaves no gap, nor the space that set it off from the text.
      last++;
    } else if (last == text.size()) {
      if (!line.empty() && !line.back().has_icon) line.back().text = trim_end(line.back().text);
    }
    at = k;
  }
  push_text(line, text.substr(last), style);
}

// Splits inline HTML and, for markdown labels, emphasis markers into styled spans.
void label_layouter::inline_markup(std::vector<span>& line, ustring_view source, bool markdown, int base) const {
  int style = base;
  int marked = 0;
  ustring text;
  std::size_t i = 0;
  const std::size_t n = source.size();
  auto flush = [&]() {
    push_icons(line, text, style);
    text.clear();
  };
  while (i < n) {
    const char16_t ch = source[i];
    if (ch == '<') {
      ustring name;
      const std::size_t end = match_tag(source, i, name);
      if (end != ustring::npos) {
        std::string tag = to_utf8(name);
        for (char& c : tag) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        const bool closing = i + 1 < n && source[i + 1] == '/';
        const int bit = tag == "b" || tag == "strong" ? text_style::bold : tag == "i" || tag == "em" ? text_style::italic : 0;
        if (bit) {
          flush();
          style = closing ? (style & ~bit) | (base & bit) : style | bit;
        }
        i = end;
        continue;
      }
    } else if (markdown && (ch == '*' || ch == '_')) {
      const bool dbl = i + 1 < n && source[i + 1] == ch;
      const int bit = dbl ? text_style::bold : text_style::italic;
      const std::size_t len = dbl ? 2 : 1;
      const bool open = (marked & bit) == 0;
      const int prev = i > 0 ? source[i - 1] : ' ';
      const int next = i + len < n ? source[i + len] : ' ';
      const ustring needle = dbl ? ustring{ch, ch} : ustring{ch};
      const bool can_open = open && !is_space(next) && source.find(needle, i + len) != ustring_view::npos;
      const bool can_close = !open && !is_space(prev);
      const bool wordy = ch == '_' && is_word(prev) && is_word(next);
      if ((can_open || can_close) && !wordy) {
        flush();
        marked = can_open ? marked | bit : marked & ~bit;
        style = can_open ? style | bit : (style & ~bit) | (base & bit);
        i += len;
        continue;
      }
    }
    text += ch;
    i++;
  }
  flush();
}

double label_layouter::measure_spans(std::vector<span>& line, double size) const {
  double total = 0;
  for (span& s : line) {
    s.width = s.has_icon ? size * 1.25 : measurer_.width(s.text, size, s.style);
    total += s.width;
  }
  return total;
}

void label_layouter::wrap(const std::vector<span>& line, double size, double max, std::vector<std::vector<span>>& out,
                          std::vector<double>& widths) const {
  std::vector<span> current;
  double width = 0;
  auto commit = [&]() {
    if (!current.empty() && !current.back().has_icon) {
      span& last = current.back();
      const ustring trimmed = trim_end(last.text);
      if (trimmed != last.text) {
        last.text = trimmed;
        const double w = measurer_.width(trimmed, size, last.style);
        width += w - last.width;
        last.width = w;
      }
    }
    out.push_back(current);
    widths.push_back(width);
    current.clear();
    width = 0;
  };
  for (const span& sp : line) {
    if (sp.has_icon) {
      const double w = size * 1.25;
      if (width > 0 && width + w > max) commit();
      span copy = sp;
      copy.width = w;
      current.push_back(std::move(copy));
      width += w;
      continue;
    }
    for (const ustring& piece : split_pieces(sp.text)) {
      ustring part = piece;
      if (width > 0 && width + measurer_.width(trim_end(part), size, sp.style) > max) {
        commit();
        part = trim_start(part);
      }
      if (!current.empty() && current.back().style == sp.style && !current.back().has_icon) {
        span& last = current.back();
        last.text += part;
        last.width = measurer_.width(last.text, size, last.style);
        width = 0;
        for (const span& s : current) width += s.width;
      } else {
        span s;
        s.text = part;
        s.style = sp.style;
        s.width = measurer_.width(part, size, sp.style);
        width += s.width;
        current.push_back(std::move(s));
      }
    }
  }
  if (!current.empty() || out.empty()) commit();
}

label label_layouter::layout(ustring_view raw, bool markdown, double size, double max_width, int base) const {
  label result;
  if (raw.empty()) return result;
  const double line_height = js_round(size * kLineHeight);

  if (!has_markup(raw) && raw.find(u'&') == ustring_view::npos && raw.find(char16_t(0xFB02)) == ustring_view::npos) {
    const ustring text = trim(raw);
    const double width = measurer_.width(text, size, base);
    span s;
    s.text = text;
    s.style = base;
    s.width = width;
    if (width <= max_width || !(text.find(u' ') != ustring::npos || contains_cjk(text))) {
      result.lines = {{s}};
      result.widths = {width};
      result.width = width;
      result.height = line_height;
      result.size = size;
      result.line_height = line_height;
      return result;
    }
    wrap({s}, size, max_width, result.lines, result.widths);
    double widest = 0;
    for (double w : result.widths)
      if (w > widest) widest = w;
    result.width = widest;
    result.height = static_cast<double>(result.lines.size()) * line_height;
    result.size = size;
    result.line_height = line_height;
    return result;
  }

  ustring source;
  if (markdown) {
    ustring unified;
    for (std::size_t i = 0; i < raw.size(); i++) {
      if (raw[i] == '\r') {
        unified += u'\n';
        if (i + 1 < raw.size() && raw[i + 1] == '\n') i++;
      } else {
        unified += raw[i];
      }
    }
    source = trim(dedent(unified));
  } else {
    source = trim(raw);
  }
  for (const ustring& part : split_breaks(source)) {
    std::vector<span> line;
    inline_markup(line, trim(part), markdown, base);
    const double w = measure_spans(line, size);
    if (w > max_width) {
      wrap(line, size, max_width, result.lines, result.widths);
    } else {
      result.lines.push_back(std::move(line));
      result.widths.push_back(w);
    }
  }
  double width = 0;
  for (double w : result.widths)
    if (w > width) width = w;
  result.width = width;
  result.height = static_cast<double>(result.lines.size()) * line_height;
  result.size = size;
  result.line_height = line_height;
  return result;
}

}  // namespace pele
