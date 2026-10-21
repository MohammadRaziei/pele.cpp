#include "yaml.h"

#include <cmath>
#include <cstdlib>
#include <memory>

#include "chars.h"
#include "js_math.h"

namespace pele {

yaml_value yaml_value::boolean(bool v) {
  yaml_value r;
  r.kind_ = kind::boolean;
  r.boolean_ = v;
  return r;
}

yaml_value yaml_value::number(double v) {
  yaml_value r;
  r.kind_ = kind::number;
  r.number_ = v;
  return r;
}

yaml_value yaml_value::string(ustring v) {
  yaml_value r;
  r.kind_ = kind::string;
  r.string_ = std::move(v);
  return r;
}

yaml_value yaml_value::array(std::vector<yaml_value> items) {
  yaml_value r;
  r.kind_ = kind::array;
  r.items_ = std::move(items);
  return r;
}

yaml_value yaml_value::object() {
  yaml_value r;
  r.kind_ = kind::object;
  return r;
}

const yaml_value* yaml_value::find(ustring_view key) const {
  for (std::size_t i = 0; i < keys_.size(); i++)
    if (keys_[i] == key) return &values_[i];
  return nullptr;
}

yaml_value* yaml_value::find(ustring_view key) {
  for (std::size_t i = 0; i < keys_.size(); i++)
    if (keys_[i] == key) return &values_[i];
  return nullptr;
}

namespace {

// Whether a key is an array index, which JavaScript lists first and in numeric order.
bool is_array_index(ustring_view key, double& value) {
  if (key.empty() || key.size() > 10) return false;
  if (key.size() > 1 && key[0] == '0') return false;
  double v = 0;
  for (char16_t c : key) {
    if (!is_digit(c)) return false;
    v = v * 10 + (c - '0');
  }
  if (v >= 4294967295.0) return false;
  value = v;
  return true;
}

}  // namespace

void yaml_value::set(ustring key, yaml_value value) {
  for (std::size_t i = 0; i < keys_.size(); i++)
    if (keys_[i] == key) {
      values_[i] = std::move(value);
      return;
    }
  double index = 0;
  if (is_array_index(key, index)) {
    std::size_t at = 0;
    while (at < keys_.size()) {
      double other = 0;
      if (!is_array_index(keys_[at], other) || other > index) break;
      at++;
    }
    keys_.insert(keys_.begin() + at, std::move(key));
    values_.insert(values_.begin() + at, std::move(value));
  } else {
    keys_.push_back(std::move(key));
    values_.push_back(std::move(value));
  }
}

bool yaml_value::truthy() const {
  switch (kind_) {
    case kind::null: return false;
    case kind::boolean: return boolean_;
    case kind::number: return !(number_ == 0 || std::isnan(number_));
    case kind::string: return !string_.empty();
    default: return true;
  }
}

ustring yaml_value::to_js_string() const {
  switch (kind_) {
    case kind::null: return u"null";
    case kind::boolean: return boolean_ ? u"true" : u"false";
    case kind::number: return ascii(js_number_string(number_));
    case kind::string: return string_;
    case kind::array: {
      ustring out;
      for (std::size_t i = 0; i < items_.size(); i++) {
        if (i > 0) out += u',';
        if (!items_[i].is_null()) out += items_[i].to_js_string();
      }
      return out;
    }
    default: return u"[object Object]";
  }
}

namespace {

// Thrown when a quoted scalar or flow collection runs past the text read so far.
class incomplete : public std::runtime_error {
 public:
  explicit incomplete(const std::string& message) : std::runtime_error(message) {}
};

constexpr int kMaxDepth = 64;

// The code unit at i, or -1 past the end, as s[i] is undefined in JavaScript.
int at(ustring_view s, std::size_t i) { return i < s.size() ? s[i] : -1; }

bool is_blank_ch(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

ustring slice(ustring_view s, std::size_t from, std::size_t to) {
  if (from > s.size()) from = s.size();
  if (to > s.size()) to = s.size();
  return from < to ? ustring(s.substr(from, to - from)) : ustring();
}

class reader {
 public:
  explicit reader(ustring_view src) {
    ustring unified;
    for (std::size_t i = 0; i < src.size(); i++) {
      if (src[i] == '\r') {
        unified += u'\n';
        if (i + 1 < src.size() && src[i + 1] == '\n') i++;
      } else {
        unified += src[i];
      }
    }
    std::size_t from = 0;
    for (;;) {
      const std::size_t nl = unified.find(u'\n', from);
      if (nl == ustring::npos) {
        lines.push_back(unified.substr(from));
        break;
      }
      lines.push_back(unified.substr(from, nl - from));
      from = nl + 1;
    }
  }

  void skip_blank() {
    while (i < lines.size()) {
      const ustring t = trim(lines[i]);
      if (!t.empty() && t[0] != '#' && t != u"---") break;
      i++;
    }
  }

  std::size_t indent() const {
    const ustring& line = lines[i];
    std::size_t k = 0;
    while (k < line.size() && line[k] == ' ') k++;
    if (at(line, k) == '\t') throw yaml_error("YAML: tabs are not allowed for indentation");
    return k;
  }

  std::vector<ustring> lines;
  std::size_t i = 0;
};

struct key_split {
  ustring key;
  std::size_t length;
};

bool is_key_start(int c) {
  if (is_space(c)) return true;
  switch (c) {
    case '#': case ':': case ',': case '[': case ']': case '{': case '}': case '&': case '*': case '!': case '|': case '>': case '\'': case '"': case '%': case '@': case '`':
      return true;
    default:
      return false;
  }
}

// Splits `key: rest` at the start of a line. Gives the raw key and the offset where the value starts.
bool split_key(const ustring& line, key_split& out) {
  const int first = at(line, 0);
  std::size_t end;
  std::size_t colon;
  if (first == '"' || first == '\'') {
    std::size_t i = 1;
    for (;;) {
      if (i >= line.size()) return false;
      const int c = line[i];
      if (first == '"' && c == '\\') {
        i += 2;
      } else if (c == first) {
        if (first == '"' || at(line, i + 1) != '\'') break;
        i += 2;
      } else {
        i++;
      }
    }
    end = i + 1;
    colon = end;
    while (is_blank_ch(at(line, colon))) colon++;
    if (at(line, colon) != ':') return false;
  } else {
    if (first == -1 || is_key_start(first)) return false;
    if (first == '-' && (line.size() < 2 || is_blank_ch(line[1]) || line[1] == ':' || line[1] == '#')) return false;
    colon = 1;
    while (colon < line.size() && line[colon] != ':' && line[colon] != '#') colon++;
    if (at(line, colon) != ':') return false;
    end = colon;
    while (is_blank_ch(at(line, end - 1))) end--;
  }
  std::size_t length = colon + 1;
  if (length < line.size() && !is_blank_ch(line[length])) return false;
  while (is_blank_ch(at(line, length))) length++;
  out.key = slice(line, 0, end);
  out.length = length;
  return true;
}

bool all_digits(ustring_view s, std::size_t from, std::size_t to) {
  if (from >= to) return false;
  for (std::size_t i = from; i < to; i++)
    if (!is_digit(s[i])) return false;
  return true;
}

// /^[-+]?(?:0|[1-9][0-9]*)$/
bool is_int(ustring_view s) {
  std::size_t i = 0;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
  if (i >= s.size()) return false;
  if (s[i] == '0') return i + 1 == s.size();
  return s[i] >= '1' && s[i] <= '9' && all_digits(s, i, s.size());
}

// /^[-+]?(?:[0-9][0-9]*(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?$/
bool is_float(ustring_view s) {
  std::size_t i = 0;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
  if (i >= s.size()) return false;
  if (is_digit(s[i])) {
    while (i < s.size() && is_digit(s[i])) i++;
    if (i < s.size() && s[i] == '.') {
      i++;
      while (i < s.size() && is_digit(s[i])) i++;
    }
  } else if (s[i] == '.') {
    i++;
    const std::size_t from = i;
    while (i < s.size() && is_digit(s[i])) i++;
    if (i == from) return false;
  } else {
    return false;
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
    if (!all_digits(s, i, s.size())) return false;
    return true;
  }
  return i == s.size();
}

// /^[-+]?0x[0-9a-fA-F]+$/, 0o[0-7]+ or 0b[01]+: gives the radix, or 0.
int radix_of(ustring_view s) {
  std::size_t i = 0;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
  if (s.size() < i + 3 || s[i] != '0') return 0;
  const char16_t k = s[i + 1];
  const std::size_t from = i + 2;
  if (k == 'x') {
    for (std::size_t j = from; j < s.size(); j++)
      if (!is_hex(s[j])) return 0;
    return 16;
  }
  if (k == 'o') {
    for (std::size_t j = from; j < s.size(); j++)
      if (s[j] < '0' || s[j] > '7') return 0;
    return 8;
  }
  if (k == 'b') {
    for (std::size_t j = from; j < s.size(); j++)
      if (s[j] != '0' && s[j] != '1') return 0;
    return 2;
  }
  return 0;
}

double digits_value(ustring_view s, std::size_t from, int radix) {
  double v = 0;
  for (std::size_t i = from; i < s.size(); i++) {
    const int c = s[i];
    const int d = c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
    v = v * radix + d;
  }
  return v;
}

double to_double(ustring_view s) { return std::strtod(to_utf8(s).c_str(), nullptr); }

yaml_value scalar(ustring_view raw) {
  const ustring s = trim(raw);
  if (s.empty() || s == u"~" || s == u"null" || s == u"Null" || s == u"NULL") return yaml_value();
  if (s == u"true" || s == u"True" || s == u"TRUE") return yaml_value::boolean(true);
  if (s == u"false" || s == u"False" || s == u"FALSE") return yaml_value::boolean(false);
  if (is_int(s) || is_float(s)) return yaml_value::number(to_double(s));
  if (const int radix = radix_of(s)) {
    const bool neg = s[0] == '-';
    const std::size_t from = (neg || s[0] == '+' ? 1 : 0) + 2;
    const double v = digits_value(s, from, radix);
    return yaml_value::number(neg ? -v : v);
  }
  {
    // /^[-+]?\.(?:inf|Inf|INF)$/
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
    const ustring rest = s.substr(i);
    if (rest == u".inf" || rest == u".Inf" || rest == u".INF") return yaml_value::number(s[0] == '-' ? -INFINITY : INFINITY);
  }
  if (s == u".nan" || s == u".NaN" || s == u".NAN") return yaml_value::number(std::nan(""));
  return yaml_value::string(s);
}

// Folds the line breaks of a scalar: blanks around a break go, one break becomes a space, and
// each further break stays.
ustring fold(const ustring& text) {
  if (text.find(u'\n') == ustring::npos) return text;
  std::vector<ustring> lines;
  std::size_t from = 0;
  for (;;) {
    const std::size_t nl = text.find(u'\n', from);
    if (nl == ustring::npos) {
      lines.push_back(text.substr(from));
      break;
    }
    lines.push_back(text.substr(from, nl - from));
    from = nl + 1;
  }
  const std::size_t last = lines.size() - 1;
  for (std::size_t i = 0; i <= last; i++) {
    const ustring& line = lines[i];
    std::size_t a = 0;
    std::size_t b = line.size();
    if (i > 0)
      while (a < b && (line[a] == 32 || line[a] == 9)) a++;
    if (i < last)
      while (b > a && (line[b - 1] == 32 || line[b - 1] == 9)) b--;
    lines[i] = line.substr(a, b - a);
  }
  ustring joined;
  for (std::size_t i = 0; i < lines.size(); i++) {
    if (i > 0) joined += u'\n';
    joined += lines[i];
  }
  ustring out;
  for (std::size_t i = 0; i < joined.size();) {
    if (joined[i] != '\n') {
      out += joined[i++];
      continue;
    }
    std::size_t j = i;
    while (j < joined.size() && joined[j] == '\n') j++;
    const std::size_t n = j - i;
    if (n == 1) out += u' ';
    else out += ustring(n - 1, u'\n');
    i = j;
  }
  return out;
}

// parseInt(text, 16) as JavaScript reads it: leading blanks, a sign, an optional 0x, then the hex digits it can.
double parse_int16(ustring_view s) {
  std::size_t i = 0;
  while (i < s.size() && is_space(s[i])) i++;
  bool neg = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    neg = s[i] == '-';
    i++;
  }
  if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
  const std::size_t from = i;
  double v = 0;
  while (i < s.size() && is_hex(s[i])) {
    const int c = s[i];
    v = v * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    i++;
  }
  if (i == from) return std::nan("");
  return neg ? -v : v;
}

ustring unquote_double(const ustring& body) {
  // Remove a backslash, a line break and the blanks after it.
  ustring joined;
  for (std::size_t i = 0; i < body.size(); i++) {
    if (body[i] == '\\' && i + 1 < body.size() && body[i + 1] == '\n') {
      i += 2;
      while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) i++;
      i--;
    } else {
      joined += body[i];
    }
  }
  const ustring s = fold(joined);
  ustring out;
  for (std::size_t i = 0; i < s.size(); i++) {
    const char16_t ch = s[i];
    if (ch != '\\') {
      out += ch;
      continue;
    }
    const int e = at(s, ++i);
    if (e == 'x' || e == 'u' || e == 'U') {
      const std::size_t len = e == 'x' ? 2 : e == 'u' ? 4 : 8;
      const double cp = parse_int16(slice(s, i + 1, i + 1 + len));
      if (std::isnan(cp) || cp < 0 || cp > 0x10FFFF || cp != std::floor(cp)) {
        throw yaml_error("Invalid code point " + (std::isnan(cp) ? std::string("NaN") : js_number_string(cp)));
      }
      out += from_code_point(static_cast<unsigned long>(cp));
      i += len;
      continue;
    }
    switch (e) {
      case '0': out += u'\0'; break;
      case 'a': out += u'\x07'; break;
      case 'b': out += u'\b'; break;
      case 't': out += u'\t'; break;
      case 'n': out += u'\n'; break;
      case 'v': out += u'\v'; break;
      case 'f': out += u'\f'; break;
      case 'r': out += u'\r'; break;
      case 'e': out += u'\x1b'; break;
      case ' ': out += u' '; break;
      case '"': out += u'"'; break;
      case '/': out += u'/'; break;
      case '\\': out += u'\\'; break;
      case 'N': out += u'\x85'; break;
      case '_': out += u'\xa0'; break;
      case 'L': out += u'\u2028'; break;
      case 'P': out += u'\u2029'; break;
      default: throw yaml_error("YAML: unknown escape sequence");
    }
  }
  return out;
}

std::size_t skip(ustring_view s, std::size_t pos, bool commas) {
  while (pos < s.size()) {
    const char16_t c = s[pos];
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r' || (commas && c == ',')) {
      pos++;
    } else if (c == '#' && (pos == 0 || is_space(s[pos - 1]))) {
      const std::size_t nl = s.find(u'\n', pos);
      pos = nl == ustring_view::npos ? s.size() : nl;
    } else {
      break;
    }
  }
  return pos;
}

// Entries of a flow collection are separated by exactly one comma, as js-yaml requires.
void entry_start(int ch, bool separated) {
  if (!separated) throw yaml_error("YAML: missed comma between flow collection entries");
  if (ch == ',') throw yaml_error("YAML: expected the node content, but found ','");
}

struct flow_result {
  yaml_value value;
  std::size_t end;
};

flow_result flow(ustring_view s, std::size_t pos, const char* stops, int depth = 0) {
  if (depth > kMaxDepth) throw yaml_error("YAML: nesting is too deep");
  pos = skip(s, pos, false);
  const int ch = at(s, pos);
  if (ch == '{') {
    yaml_value out = yaml_value::object();
    pos++;
    bool separated = true;
    for (;;) {
      pos = skip(s, pos, false);
      if (pos >= s.size()) throw incomplete("YAML: unexpected end of the stream within a flow collection");
      if (s[pos] == '}') return {std::move(out), pos + 1};
      entry_start(s[pos], separated);
      flow_result key = flow(s, pos, ":,}", depth + 1);
      pos = skip(s, key.end, false);
      yaml_value value;
      if (at(s, pos) == ':') {
        flow_result v = flow(s, pos + 1, ",}", depth + 1);
        value = std::move(v.value);
        pos = v.end;
      }
      out.set(key.value.to_js_string(), std::move(value));
      pos = skip(s, pos, false);
      separated = at(s, pos) == ',';
      if (separated) pos++;
    }
  }
  if (ch == '[') {
    std::vector<yaml_value> out;
    pos++;
    bool separated = true;
    for (;;) {
      pos = skip(s, pos, false);
      if (pos >= s.size()) throw incomplete("YAML: unexpected end of the stream within a flow collection");
      if (s[pos] == ']') return {yaml_value::array(std::move(out)), pos + 1};
      entry_start(s[pos], separated);
      flow_result v = flow(s, pos, ",]", depth + 1);
      out.push_back(std::move(v.value));
      pos = skip(s, v.end, false);
      separated = at(s, pos) == ',';
      if (separated) pos++;
    }
  }
  if (ch == '"') {
    std::size_t end = pos + 1;
    while (end < s.size() && s[end] != '"') end += s[end] == '\\' ? 2 : 1;
    if (end >= s.size()) throw incomplete("YAML: unexpected end of the stream within a double quoted scalar");
    return {yaml_value::string(unquote_double(slice(s, pos + 1, end))), end + 1};
  }
  if (ch == '\'') {
    std::size_t end = pos + 1;
    while (end < s.size()) {
      if (s[end] == '\'') {
        if (at(s, end + 1) != '\'') break;
        end++;
      }
      end++;
    }
    if (end >= s.size()) throw incomplete("YAML: unexpected end of the stream within a single quoted scalar");
    const ustring folded = fold(slice(s, pos + 1, end));
    ustring out;
    for (std::size_t i = 0; i < folded.size(); i++) {
      if (folded[i] == '\'' && i + 1 < folded.size() && folded[i + 1] == '\'') i++;
      out += folded[i];
    }
    return {yaml_value::string(out), end + 1};
  }
  const std::string stop_set(stops);
  std::size_t end = pos;
  while (end < s.size()) {
    const char16_t c = s[end];
    const bool is_stop = c < 128 && stop_set.find(static_cast<char>(c)) != std::string::npos && c != 0;
    if (is_stop && (c != ':' || end + 1 >= s.size() || is_space(s[end + 1]) || s[end + 1] == ',' || s[end + 1] == '[' || s[end + 1] == ']' || s[end + 1] == '{' || s[end + 1] == '}'))
      break;
    if (c == '#' && end > pos && is_space(s[end - 1])) {
      const std::size_t nl = s.find(u'\n', end);
      return {scalar(fold(trim(slice(s, pos, end)))), nl == ustring_view::npos ? s.size() : nl};
    }
    end++;
  }
  return {scalar(fold(trim(slice(s, pos, end)))), end};
}

ustring strip_comment(const ustring& s) {
  char16_t quote = 0;
  for (std::size_t i = 0; i < s.size(); i++) {
    const char16_t c = s[i];
    if (quote) {
      if (c == '\\' && quote == '"') i++;
      else if (c == quote) quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
      return s.substr(0, i);
    }
  }
  return s;
}

bool contains_char(const ustring& s, char16_t c) { return s.find(c) != ustring::npos; }

ustring repeat_newline(std::size_t n) { return ustring(n, u'\n'); }

ustring block_scalar(reader& r, const ustring& header, std::size_t parent_indent) {
  const bool folded = header[0] == '>';
  const char chomp = contains_char(header, '-') ? '-' : contains_char(header, '+') ? '+' : '\0';
  // /[1-9]/.exec(header): the first such digit anywhere in the header, even in its comment.
  long long explicit_digit = 0;
  for (char16_t c : header)
    if (c >= '1' && c <= '9') {
      explicit_digit = c - '0';
      break;
    }
  long long indent = explicit_digit ? static_cast<long long>(parent_indent) + explicit_digit : -1;
  std::vector<ustring> out;
  while (r.i < r.lines.size()) {
    const ustring& line = r.lines[r.i];
    if (trim(line).empty()) {
      out.emplace_back();
      r.i++;
      continue;
    }
    const long long k = static_cast<long long>(line.size() - trim_start(line).size());
    if (indent == -1) {
      if (k <= static_cast<long long>(parent_indent)) break;
      indent = k;
    }
    if (k < indent) break;
    out.push_back(line.size() >= static_cast<std::size_t>(indent) ? line.substr(static_cast<std::size_t>(indent)) : ustring());
    r.i++;
  }
  std::size_t trailing = 0;
  while (!out.empty() && out.back().empty()) {
    out.pop_back();
    trailing++;
  }
  ustring text;
  if (folded) {
    for (std::size_t i = 0; i < out.size(); i++) {
      const ustring& cur = out[i];
      if (i > 0) {
        const ustring& prev = out[i - 1];
        if (cur.empty()) text += u'\n';
        else if (!prev.empty()) text += ((!prev.empty() && (prev[0] == ' ' || prev[0] == '\t')) || (!cur.empty() && (cur[0] == ' ' || cur[0] == '\t'))) ? u'\n' : u' ';
      }
      text += cur;
    }
  } else {
    for (std::size_t i = 0; i < out.size(); i++) {
      if (i > 0) text += u'\n';
      text += out[i];
    }
  }
  if (out.empty()) return chomp == '+' ? repeat_newline(trailing) : ustring();
  if (chomp == '-') return text;
  if (chomp == '+') return text + repeat_newline(trailing + 1);
  return text + u'\n';
}

// /^[|>][-+0-9]*\s*(?:#.*)?$/
bool is_block_header(const ustring& rest) {
  if (rest.empty() || (rest[0] != '|' && rest[0] != '>')) return false;
  std::size_t i = 1;
  while (i < rest.size() && (rest[i] == '-' || rest[i] == '+' || is_digit(rest[i]))) i++;
  while (i < rest.size() && is_space(rest[i])) i++;
  if (i >= rest.size()) return true;
  if (rest[i] != '#') return false;
  for (std::size_t k = i + 1; k < rest.size(); k++)
    if (is_line_end(rest[k])) return false;
  return true;
}

yaml_value block(reader& r, long long min_indent, int depth = 0);

yaml_value inline_value(reader& r, const ustring& rest, std::size_t indent_in) {
  const long long indent = static_cast<long long>(indent_in);
  const int first = at(rest, 0);
  if (first == '|' || first == '>') {
    if (is_block_header(rest)) return yaml_value::string(block_scalar(r, rest, indent_in));
  }
  if (first == '{' || first == '[' || first == '"' || first == '\'') {
    // The value may continue on later lines. Read them in growing batches until it closes.
    const std::size_t start = r.i;
    ustring text = rest;
    std::size_t taken = 0;
    std::size_t batch = 1;
    for (;;) {
      try {
        flow_result f = flow(text, 0, "");
        std::size_t used = 0;
        for (std::size_t k = text.find(u'\n'); k != ustring::npos && k < f.end; k = text.find(u'\n', k + 1)) used++;
        const std::size_t line_end = text.find(u'\n', f.end);
        if (!trim(strip_comment(slice(text, f.end, line_end == ustring::npos ? text.size() : line_end))).empty())
          throw yaml_error("YAML: unexpected content after a value");
        r.i = start + used;
        return std::move(f.value);
      } catch (const incomplete& err) {
        if (start + taken >= r.lines.size()) throw yaml_error(err.what());
        const std::size_t count = std::min(batch, r.lines.size() - (start + taken));
        for (std::size_t k = 0; k < count; k++) {
          text += u'\n';
          text += r.lines[start + taken + k];
        }
        taken += count;
        batch *= 2;
      }
    }
  }
  ustring text = trim(strip_comment(rest));
  while (r.i < r.lines.size()) {
    const ustring& line = r.lines[r.i];
    if (trim(line).empty()) {
      std::size_t j = r.i;
      while (j < r.lines.size() && trim(r.lines[j]).empty()) j++;
      if (j >= r.lines.size() || static_cast<long long>(r.lines[j].size() - trim_start(r.lines[j]).size()) <= indent) break;
      text += repeat_newline(j - r.i);
      r.i = j;
      continue;
    }
    if (static_cast<long long>(line.size() - trim_start(line).size()) <= indent) break;
    const ustring more = trim(strip_comment(line));
    text += (!text.empty() && text.back() == '\n') ? ustring() : ustring(u" ");
    text += more;
    r.i++;
  }
  return scalar(text);
}

bool is_dash_item(const ustring& s) { return s == u"-" || (s.size() >= 2 && s[0] == '-' && s[1] == ' '); }

yaml_value block(reader& r, long long min_indent, int depth) {
  if (depth > kMaxDepth) throw yaml_error("YAML: nesting is too deep");
  r.skip_blank();
  if (r.i >= r.lines.size()) return yaml_value();
  const std::size_t indent = r.indent();
  if (static_cast<long long>(indent) < min_indent) return yaml_value();
  const ustring line = r.lines[r.i].substr(indent);

  if (is_dash_item(line)) {
    std::vector<yaml_value> out;
    while (r.i < r.lines.size()) {
      r.skip_blank();
      if (r.i >= r.lines.size() || r.indent() != indent) break;
      const ustring cur = r.lines[r.i].substr(indent);
      if (!is_dash_item(cur)) break;
      const ustring rest = cur.substr(1);
      const std::size_t pad = rest.size() - trim_start(rest).size() + 1;
      const ustring body = trim(strip_comment(rest));
      key_split ks;
      if (body.empty()) {
        r.i++;
        out.push_back(block(r, static_cast<long long>(indent) + 1, depth + 1));
      } else if (split_key(body, ks)) {
        r.lines[r.i] = ustring(indent + pad, u' ') + trim_start(rest);
        out.push_back(block(r, static_cast<long long>(indent + pad), depth + 1));
      } else {
        r.i++;
        out.push_back(inline_value(r, trim(rest), indent));
      }
    }
    return yaml_value::array(std::move(out));
  }

  key_split first_key;
  if (!split_key(line, first_key)) {
    r.i++;
    // indent - 1 may be -1 for a value at the left edge: nothing is less indented than that.
    return inline_value(r, trim(line), indent == 0 ? static_cast<std::size_t>(-1) : indent - 1);
  }

  yaml_value out = yaml_value::object();
  while (r.i < r.lines.size()) {
    r.skip_blank();
    if (r.i >= r.lines.size()) break;
    const std::size_t k = r.indent();
    if (k < indent) break;
    if (k > indent) throw yaml_error("YAML: bad indentation of a mapping entry");
    const ustring cur = r.lines[r.i].substr(indent);
    key_split km;
    if (!split_key(cur, km)) throw yaml_error("YAML: can not read a block mapping entry");
    const ustring key = flow(km.key, 0, "").value.to_js_string();
    const ustring rest = cur.substr(std::min(km.length, cur.size()));
    r.i++;
    if (trim(strip_comment(rest)).empty()) {
      r.skip_blank();
      if (r.i < r.lines.size()) {
        const std::size_t next = r.indent();
        const ustring next_line = r.lines[r.i].substr(next);
        const bool seq = is_dash_item(next_line);
        out.set(key, next > indent || (seq && next == indent) ? block(r, static_cast<long long>(seq ? indent : indent + 1), depth + 1) : yaml_value());
      } else {
        out.set(key, yaml_value());
      }
    } else {
      out.set(key, inline_value(r, trim(rest), indent));
    }
  }
  return out;
}

}  // namespace

yaml_value parse_yaml(ustring_view src) {
  const ustring trimmed = trim(src);
  if (!trimmed.empty() && (trimmed[0] == '{' || trimmed[0] == '[')) {
    try {
      flow_result f = flow(trimmed, 0, "");
      if (skip(trimmed, f.end, false) == trimmed.size()) return std::move(f.value);
    } catch (const incomplete& err) {
      throw yaml_error(err.what());
    }
  }
  reader r{src};
  yaml_value value = block(r, 0);
  r.skip_blank();
  if (r.i < r.lines.size() && trim(r.lines[r.i]) != u"...") throw yaml_error("YAML: end of the stream or a document separator is expected");
  return value;
}

// ---- JSON.parse ----

namespace {

class json_reader {
 public:
  explicit json_reader(ustring_view s) : s_(s) {}

  yaml_value parse() {
    skip_ws();
    yaml_value v = value(0);
    skip_ws();
    if (i_ < s_.size()) fail();
    return v;
  }

 private:
  [[noreturn]] void fail() const { throw yaml_error("Unexpected token in JSON at position " + std::to_string(i_)); }

  void skip_ws() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++;
  }

  bool literal(const char* word) {
    std::size_t k = 0;
    for (; word[k]; k++)
      if (i_ + k >= s_.size() || s_[i_ + k] != static_cast<unsigned char>(word[k])) return false;
    i_ += k;
    return true;
  }

  yaml_value value(int depth) {
    if (depth > 4000) throw yaml_error("Maximum call stack size exceeded");
    if (i_ >= s_.size()) fail();
    const char16_t c = s_[i_];
    if (c == '{') return object(depth);
    if (c == '[') return array(depth);
    if (c == '"') return yaml_value::string(string());
    if (c == '-' || is_digit(c)) return number();
    if (literal("true")) return yaml_value::boolean(true);
    if (literal("false")) return yaml_value::boolean(false);
    if (literal("null")) return yaml_value();
    fail();
  }

  yaml_value object(int depth) {
    yaml_value out = yaml_value::object();
    i_++;
    skip_ws();
    if (i_ < s_.size() && s_[i_] == '}') {
      i_++;
      return out;
    }
    for (;;) {
      skip_ws();
      if (i_ >= s_.size() || s_[i_] != '"') fail();
      ustring key = string();
      skip_ws();
      if (i_ >= s_.size() || s_[i_] != ':') fail();
      i_++;
      skip_ws();
      out.set(std::move(key), value(depth + 1));
      skip_ws();
      if (i_ < s_.size() && s_[i_] == ',') {
        i_++;
        continue;
      }
      if (i_ < s_.size() && s_[i_] == '}') {
        i_++;
        return out;
      }
      fail();
    }
  }

  yaml_value array(int depth) {
    std::vector<yaml_value> out;
    i_++;
    skip_ws();
    if (i_ < s_.size() && s_[i_] == ']') {
      i_++;
      return yaml_value::array(std::move(out));
    }
    for (;;) {
      skip_ws();
      out.push_back(value(depth + 1));
      skip_ws();
      if (i_ < s_.size() && s_[i_] == ',') {
        i_++;
        continue;
      }
      if (i_ < s_.size() && s_[i_] == ']') {
        i_++;
        return yaml_value::array(std::move(out));
      }
      fail();
    }
  }

  ustring string() {
    ustring out;
    i_++;
    for (;;) {
      if (i_ >= s_.size()) fail();
      const char16_t c = s_[i_++];
      if (c == '"') return out;
      if (c < 0x20) {
        i_--;
        fail();
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= s_.size()) fail();
      const char16_t e = s_[i_++];
      switch (e) {
        case '"': out += u'"'; break;
        case '\\': out += u'\\'; break;
        case '/': out += u'/'; break;
        case 'b': out += u'\b'; break;
        case 'f': out += u'\f'; break;
        case 'n': out += u'\n'; break;
        case 'r': out += u'\r'; break;
        case 't': out += u'\t'; break;
        case 'u': {
          unsigned v = 0;
          for (int k = 0; k < 4; k++) {
            if (i_ >= s_.size() || !is_hex(s_[i_])) fail();
            const int d = s_[i_] <= '9' ? s_[i_] - '0' : (s_[i_] | 0x20) - 'a' + 10;
            v = v * 16 + d;
            i_++;
          }
          out += static_cast<char16_t>(v);
          break;
        }
        default:
          i_--;
          fail();
      }
    }
  }

  yaml_value number() {
    const std::size_t start = i_;
    if (s_[i_] == '-') i_++;
    if (i_ >= s_.size() || !is_digit(s_[i_])) fail();
    if (s_[i_] == '0') {
      i_++;
    } else {
      while (i_ < s_.size() && is_digit(s_[i_])) i_++;
    }
    if (i_ < s_.size() && s_[i_] == '.') {
      i_++;
      if (i_ >= s_.size() || !is_digit(s_[i_])) fail();
      while (i_ < s_.size() && is_digit(s_[i_])) i_++;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      i_++;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) i_++;
      if (i_ >= s_.size() || !is_digit(s_[i_])) fail();
      while (i_ < s_.size() && is_digit(s_[i_])) i_++;
    }
    return yaml_value::number(to_double(s_.substr(start, i_ - start)));
  }

  ustring_view s_;
  std::size_t i_ = 0;
};

}  // namespace

yaml_value parse_json(ustring_view src) { return json_reader(src).parse(); }

}  // namespace pele
