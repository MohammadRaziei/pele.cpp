#include "url.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

#include "chars.h"
#include "entities.h"
#include "svg.h"

namespace pele {
namespace {

const ustring kBlank = u"about:blank";

bool is_ctrl(int c) { return c <= 0x1f || (c >= 0x7f && c <= 0x9f) || (c >= 0x2000 && c <= 0x200d) || c == 0xfeff; }

ustring remove_ctrl(const ustring& s) {
  ustring out;
  for (char16_t c : s)
    if (!is_ctrl(c)) out += c;
  return out;
}

int hex_val(int c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

int unit_at(const ustring& s, std::size_t i) { return i < s.size() ? s[i] : -1; }

// Reads the byte of a `%XX` at i, if there is one.
bool percent_byte(const ustring& s, std::size_t i, int& b) {
  if (unit_at(s, i) != '%' || !is_hex(unit_at(s, i + 1)) || !is_hex(unit_at(s, i + 2))) return false;
  b = hex_val(s[i + 1]) * 16 + hex_val(s[i + 2]);
  return true;
}

// decodeURIComponent. Sets `ok` to false where JavaScript would throw.
ustring decode_component(const ustring& s, bool& ok) {
  ok = true;
  if (s.find(u'%') == ustring::npos) return s;
  ustring out;
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] != '%') {
      out += s[i++];
      continue;
    }
    int b = 0;
    if (!percent_byte(s, i, b)) {
      ok = false;
      return s;
    }
    if (b < 0x80) {
      out += static_cast<char16_t>(b);
      i += 3;
      continue;
    }
    int extra;
    unsigned long cp;
    if (b >= 0xC2 && b <= 0xDF) {
      extra = 1;
      cp = b & 0x1F;
    } else if (b >= 0xE0 && b <= 0xEF) {
      extra = 2;
      cp = b & 0x0F;
    } else if (b >= 0xF0 && b <= 0xF4) {
      extra = 3;
      cp = b & 0x07;
    } else {
      ok = false;
      return s;
    }
    std::size_t k = i + 3;
    for (int e = 0; e < extra; e++, k += 3) {
      int c = 0;
      if (!percent_byte(s, k, c) || (c & 0xC0) != 0x80) {
        ok = false;
        return s;
      }
      cp = (cp << 6) | (c & 0x3F);
    }
    const unsigned long min = extra == 1 ? 0x80 : extra == 2 ? 0x800 : 0x10000;
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      ok = false;
      return s;
    }
    out += from_code_point(cp);
    i = k;
  }
  return out;
}

// Number(text) for what `\w+` can hold; NaN where it is not a number.
double word_number(const ustring& w) {
  bool digits = !w.empty();
  for (char16_t c : w)
    if (!is_digit(c)) digits = false;
  auto value = [](const ustring& s, std::size_t from, int radix) {
    double v = 0;
    for (std::size_t i = from; i < s.size(); i++) v = v * radix + hex_val(s[i]);
    return v;
  };
  if (digits) return value(w, 0, 10);
  if (w.size() > 2 && w[0] == '0') {
    const char16_t k = w[1] | 0x20;
    const int radix = k == 'x' ? 16 : k == 'o' ? 8 : k == 'b' ? 2 : 0;
    if (radix) {
      bool ok = true;
      for (std::size_t i = 2; i < w.size(); i++) {
        if (!is_hex(w[i]) || hex_val(w[i]) >= radix) ok = false;
      }
      if (ok) return value(w, 2, radix);
    }
  }
  // digits, `e`, digits
  const std::size_t e = w.find_first_of(u"eE");
  if (e != ustring::npos && e > 0 && e + 1 < w.size()) {
    bool ok = true;
    for (std::size_t i = 0; i < w.size(); i++)
      if (i != e && !is_digit(w[i])) ok = false;
    if (ok) return std::strtod(to_utf8(w).c_str(), nullptr);
  }
  if (w == u"Infinity") return INFINITY;
  return std::nan("");
}

char16_t from_char_code(double v) {
  if (std::isnan(v) || std::isinf(v)) return 0;
  const double t = std::trunc(v);
  double m = std::fmod(t, 65536.0);
  if (m < 0) m += 65536.0;
  return static_cast<char16_t>(m);
}

bool starts_ci(const ustring& s, std::size_t i, const char* word) {
  for (std::size_t k = 0; word[k]; k++) {
    const int c = unit_at(s, i + k);
    if (c < 0 || (c | 0x20) != static_cast<unsigned char>(word[k]) || (c >= 128)) return false;
  }
  return true;
}

// Whether `\\` or `%5c`, then `%6e`, `%72`, `%74`, `n`, `r` or `t`, is at i; gives its length.
std::size_t ws_escape_at(const ustring& s, std::size_t i) {
  std::size_t k;
  if (unit_at(s, i) == '\\') k = i + 1;
  else if (unit_at(s, i) == '%' && unit_at(s, i + 1) == '5' && (unit_at(s, i + 2) | 0x20) == 'c') k = i + 3;
  else return 0;
  const int c = unit_at(s, k);
  if (c == 'n' || c == 'r' || c == 't') return k + 1 - i;
  if (c == '%') {
    if (unit_at(s, k + 1) == '6' && (unit_at(s, k + 2) | 0x20) == 'e') return k + 3 - i;
    if (unit_at(s, k + 1) == '7' && (unit_at(s, k + 2) == '2' || unit_at(s, k + 2) == '4')) return k + 3 - i;
  }
  return 0;
}

// `&#\w+;?` as a run, or 0.
std::size_t entity_at(const ustring& s, std::size_t i, ustring& word) {
  if (unit_at(s, i) != '&' || unit_at(s, i + 1) != '#') return 0;
  std::size_t k = i + 2;
  while (k < s.size() && is_word(s[k])) k++;
  if (k == i + 2) return 0;
  word = s.substr(i + 2, k - i - 2);
  if (unit_at(s, k) == ';') k++;
  return k - i;
}

// `&(newline|tab);`, in any case.
std::size_t ctrl_entity_at(const ustring& s, std::size_t i) {
  if (unit_at(s, i) != '&') return 0;
  if (starts_ci(s, i + 1, "newline") && unit_at(s, i + 8) == ';') return 9;
  if (starts_ci(s, i + 1, "tab") && unit_at(s, i + 4) == ';') return 5;
  return 0;
}

bool has_more(const ustring& s) {
  for (std::size_t i = 0; i < s.size(); i++) {
    ustring w;
    if (is_ctrl(s[i]) || entity_at(s, i, w) || ctrl_entity_at(s, i) || ws_escape_at(s, i)) return true;
  }
  return false;
}

ustring strip_hidden(const ustring& s) {
  // Each pattern in turn, as the original applies them one after another.
  ustring a = remove_ctrl(s);
  ustring b;
  for (std::size_t i = 0; i < a.size();) {
    ustring w;
    if (const std::size_t len = entity_at(a, i, w)) {
      b += from_char_code(word_number(w));
      i += len;
    } else {
      b += a[i++];
    }
  }
  ustring c;
  for (std::size_t i = 0; i < b.size();) {
    if (const std::size_t len = ctrl_entity_at(b, i)) i += len;
    else c += b[i++];
  }
  ustring d;
  for (std::size_t i = 0; i < c.size();) {
    if (const std::size_t len = ws_escape_at(c, i)) i += len;
    else d += c[i++];
  }
  return d;
}

ustring lower_ascii(ustring s) {
  for (char16_t& c : s)
    if (c >= 'A' && c <= 'Z') c += 32;
  return s;
}

// `^.+(:|&colon;)` with the m flag: the first line that has a colon after its first character, up to its last colon.
bool find_scheme(const ustring& s, ustring& out) {
  std::size_t ls = 0;
  for (;;) {
    std::size_t le = ls;
    while (le < s.size() && !is_line_end(s[le])) le++;
    for (std::size_t k = le; k-- > ls + 1;) {
      if (s[k] == ':') {
        out = s.substr(ls, k + 1 - ls);
        return true;
      }
      if (starts_ci(s, k, "&colon;") && k + 7 <= le) {
        out = s.substr(ls, k + 7 - ls);
        return true;
      }
    }
    if (le >= s.size()) return false;
    ls = le + 1;
  }
}

// `^([^\w]*)(javascript|data|vbscript)`
bool invalid_protocol(const ustring& p) {
  std::size_t i = 0;
  while (i < p.size() && !is_word(p[i])) i++;
  return starts_ci(p, i, "javascript") || starts_ci(p, i, "data") || starts_ci(p, i, "vbscript");
}

// A stand-in for `new URL(text).toString()` for http and https addresses: it lower-cases the
// scheme and host, drops the default port, resolves dot segments and escapes what a URL may not hold.
bool normalize_http(const ustring& text, ustring& out) {
  const std::size_t colon = text.find(u':');
  if (colon == ustring::npos) return false;
  const ustring scheme = lower_ascii(text.substr(0, colon));
  if (scheme != u"http" && scheme != u"https") return false;
  std::size_t i = colon + 1;
  while (i < text.size() && text[i] == '/') i++;
  std::size_t auth_end = i;
  while (auth_end < text.size() && text[auth_end] != '/' && text[auth_end] != '?' && text[auth_end] != '#') auth_end++;
  ustring authority = text.substr(i, auth_end - i);
  if (authority.empty()) return false;
  ustring userinfo;
  const std::size_t at = authority.rfind(u'@');
  if (at != ustring::npos) {
    userinfo = authority.substr(0, at + 1);
    authority = authority.substr(at + 1);
  }
  ustring host = lower_ascii(authority);
  const std::size_t port_at = host.rfind(u':');
  if (port_at != ustring::npos && host.find(u']') == ustring::npos) {
    const ustring port = host.substr(port_at + 1);
    if (port == (scheme == u"http" ? u"80" : u"443")) host = host.substr(0, port_at);
  }
  if (host.empty()) return false;
  // The host may not hold a character that cannot be in one (an IPv6 address in brackets is taken as it is).
  if (host[0] != '[') {
    for (char16_t c : host) {
      if (c <= 0x20 || c == 0x7f || c == '#' || c == '/' || c == ':' || c == '<' || c == '>' || c == '?' || c == '@' || c == '[' || c == '\\' || c == ']' ||
          c == '^' || c == '|')
        return false;
    }
  }
  std::size_t path_end = auth_end;
  while (path_end < text.size() && text[path_end] != '?' && text[path_end] != '#') path_end++;
  ustring path = text.substr(auth_end, path_end - auth_end);
  ustring rest = text.substr(path_end);
  auto encode = [](const ustring& s, const char* extra) {
    ustring r;
    const std::string utf8 = to_utf8(s);
    for (unsigned char c : utf8) {
      const bool bad = c <= 0x20 || c >= 0x7f || std::string(extra).find(static_cast<char>(c)) != std::string::npos;
      if (bad) {
        static const char* digits = "0123456789ABCDEF";
        r += u'%';
        r += static_cast<char16_t>(digits[c >> 4]);
        r += static_cast<char16_t>(digits[c & 15]);
      } else {
        r += static_cast<char16_t>(c);
      }
    }
    return r;
  };
  // Dot segments.
  std::vector<ustring> parts;
  std::size_t from = 0;
  for (;;) {
    const std::size_t sl = path.find(u'/', from);
    parts.push_back(path.substr(from, sl == ustring::npos ? ustring::npos : sl - from));
    if (sl == ustring::npos) break;
    from = sl + 1;
  }
  std::vector<ustring> kept;
  for (std::size_t k = 1; k < parts.size(); k++) {
    const ustring lower = lower_ascii(parts[k]);
    const bool last = k + 1 == parts.size();
    if (parts[k] == u"." || lower == u"%2e") {
      if (last) kept.emplace_back();
    } else if (parts[k] == u".." || lower == u".%2e" || lower == u"%2e." || lower == u"%2e%2e") {
      if (!kept.empty()) kept.pop_back();
      if (last) kept.emplace_back();
    } else {
      kept.push_back(parts[k]);
    }
  }
  ustring new_path;
  if (kept.empty()) new_path = u"/";
  for (const ustring& k : kept) new_path += u"/" + encode(k, "\"<>`#?{}");
  ustring tail;
  std::size_t hash = rest.find(u'#');
  ustring query = hash == ustring::npos ? rest : rest.substr(0, hash);
  ustring fragment = hash == ustring::npos ? ustring() : rest.substr(hash);
  if (!query.empty()) tail += u"?" + encode(query.substr(1), "\"<>#'");
  if (!fragment.empty()) tail += u"#" + encode(fragment.substr(1), "\"<>`");
  out = scheme + u"://" + userinfo + host + new_path + tail;
  return true;
}

}  // namespace

namespace {

ustring decode_or_same(const ustring& s) {
  bool ok = true;
  ustring out = decode_component(s, ok);
  return ok ? out : s;
}

// Removes the control characters, entity codes and escapes that could hide a scheme, and with
// `percent`, percent escapes too. Nothing for a URL wrapped too many times to be a real one.
std::optional<ustring> unwrap(const ustring& url, bool percent) {
  ustring out = percent ? decode_or_same(trim(url)) : trim(url);
  int rounds = 0;
  bool more;
  do {
    if (++rounds > 32) return std::nullopt;
    out = trim(strip_hidden(out));
    if (percent) out = decode_or_same(out);
    more = has_more(out);
  } while (more);
  return out;
}

}  // namespace

ustring sanitize_url(const ustring& url) {
  if (url.empty()) return kBlank;
  const std::optional<ustring> decoded_opt = unwrap(url, true);
  const std::optional<ustring> kept_opt = unwrap(url, false);
  if (!decoded_opt || !kept_opt || decoded_opt->empty() || kept_opt->empty()) return kBlank;
  const ustring& decoded = *decoded_opt;
  const ustring& kept = *kept_opt;
  if (decoded[0] == '.' || decoded[0] == '/') return kept;
  ustring scheme;
  if (!find_scheme(trim_start(decoded), scheme)) return kept;
  const ustring protocol = trim(lower_ascii(scheme));
  if (invalid_protocol(protocol)) return kBlank;
  ustring normalized = trim_start(kept);
  for (char16_t& c : normalized)
    if (c == '\\') c = '/';
  if (protocol == u"mailto:" || protocol.find(u"://") != ustring::npos) return normalized;
  if (protocol == u"http:" || protocol == u"https:") {
    ustring href;
    if (!normalize_http(normalized, href)) return kBlank;
    return href;
  }
  return normalized;
}

ustring safe_url(const ustring& url) { return sanitize_url(decode_entities(url)); }

namespace {

bool allowed(const ustring& url, const std::vector<std::string>& schemes) {
  // A network-path reference (`//host/path`) is not relative: it names another site.
  if (url.size() >= 2 && (url[0] == '/' || url[0] == '\\') && (url[1] == '/' || url[1] == '\\')) return false;
  // ^([a-z][a-z0-9+.-]*):
  std::size_t i = 0;
  if (i < url.size() && is_letter(url[i])) {
    i++;
    while (i < url.size() && (is_letter(url[i]) || is_digit(url[i]) || url[i] == '+' || url[i] == '.' || url[i] == '-')) i++;
    if (unit_at(url, i) == ':') {
      const std::string scheme = to_utf8(lower_ascii(url.substr(0, i)));
      if (std::find(schemes.begin(), schemes.end(), scheme) == schemes.end()) return false;
    }
  }
  return true;
}

// The decoded URL is checked too, for an app that decodes the href before opening it.
std::optional<ustring> allow(const ustring& url, const std::vector<std::string>& schemes) {
  const std::optional<ustring> decoded = unwrap(url, true);
  return decoded && allowed(url, schemes) && allowed(*decoded, schemes) ? std::optional<ustring>(url) : std::nullopt;
}

const std::vector<std::string>& default_link_schemes() {
  static const std::vector<std::string> list = {"http", "https", "mailto", "tel"};
  return list;
}

const std::vector<std::string>& default_image_schemes() {
  static const std::vector<std::string> list = {"http", "https"};
  return list;
}

}  // namespace

std::optional<ustring> link_url(const ustring& url, const render_options& options) {
  return options.links ? allow(url, options.link_schemes ? *options.link_schemes : default_link_schemes()) : std::nullopt;
}

std::optional<ustring> image_url(const ustring& url, const render_options& options) {
  return options.images ? allow(url, options.image_schemes ? *options.image_schemes : default_image_schemes()) : std::nullopt;
}

std::string rel_attr(const render_options& options) {
  const std::string rel = options.link_rel ? *options.link_rel : std::string("noopener");
  return rel.empty() ? "" : " rel=\"" + svg::esc(rel) + "\"";
}

std::string linked(const std::string& body, const ustring& url, const ustring& id, std::vector<rendered_link>& links, const render_options& options,
                   const std::string& attrs) {
  const auto href = link_url(url, options);
  if (!href) return body;
  links.push_back({id, *href, false});
  return "<a href=\"" + svg::esc(*href) + "\"" + attrs + rel_attr(options) + ">" + body + "</a>";
}

}  // namespace pele
