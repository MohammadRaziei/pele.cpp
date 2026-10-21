#include "entities.h"

#include <cstdlib>
#include <string>
#include <unordered_map>

#include "chars.h"

namespace pele {
namespace {

#include "entities_data.inc"

const std::unordered_map<std::string, ustring>& named() {
  static const std::unordered_map<std::string, ustring> table = [] {
    std::unordered_map<std::string, ustring> t;
    const std::string all(kNamedEntities);
    std::size_t from = 0;
    while (from < all.size()) {
      std::size_t comma = all.find(',', from);
      if (comma == std::string::npos) comma = all.size();
      const std::string pair = all.substr(from, comma - from);
      const std::size_t at = pair.find(':');
      t[pair.substr(0, at)] = from_code_point(std::strtoul(pair.c_str() + at + 1, nullptr, 10));
      from = comma + 1;
    }
    return t;
  }();
  return table;
}

bool lookup(ustring_view name, ustring& out) {
  const auto it = named().find(to_utf8(name));
  if (it == named().end()) return false;
  out = it->second;
  return true;
}

// The character for a code point, or `fallback` where it is not a valid one.
ustring code_point(double n, ustring_view fallback) {
  if (n > 0 && n <= 0x10ffff && !(n >= 0xd800 && n <= 0xdfff)) return from_code_point(static_cast<unsigned long>(n));
  return ustring(fallback);
}

const ustring kOpen = {0xFB02, 0x00B0};
const ustring kClose = {0x00B6, 0x00DF};

// The value of a run of digits in the given base, as a double so that long runs stay out of range.
double number(ustring_view digits, int base) {
  double v = 0;
  for (char16_t c : digits) {
    const int d = c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
    v = v * base + d;
    if (v > 1e12) v = 1e12;
  }
  return v;
}

// Mermaid's placeholders run from `ﬂ°` to `¶ß`, with a second `°` marking a numeric code.
ustring decode_placeholders(const ustring& text) {
  ustring out;
  std::size_t last = 0;
  std::size_t from = text.find(kOpen);
  while (from != ustring::npos) {
    const std::size_t stop = text.find(char16_t(0x00B6), from + 2);
    if (stop == ustring::npos) break;
    if (stop + 1 < text.size() && text[stop + 1] == 0x00DF) {
      const bool numeric = text[from + 2] == 0x00B0;
      const std::size_t body_at = from + (numeric ? 3 : 2);
      const ustring body = body_at <= stop ? text.substr(body_at, stop - body_at) : ustring();
      const ustring raw = text.substr(from, stop + 2 - from);
      out += text.substr(last, from - last);
      if (numeric) {
        // Number(body): digits give their value; anything else is not a code.
        const ustring t = trim(body);
        bool digits = !t.empty();
        for (char16_t c : t)
          if (!is_digit(c)) digits = false;
        out += digits ? code_point(number(t, 10), raw) : (t.empty() ? code_point(0, raw) : raw);
      } else {
        ustring found;
        if (lookup(body, found)) out += found;
        else out += u"&" + body + u";";
      }
      last = stop + 2;
    }
    from = text.find(kOpen, stop + 1);
  }
  return last == 0 ? text : out + text.substr(last);
}

}  // namespace

ustring decode_entities(ustring text) {
  if (text.find(kClose) != ustring::npos) text = decode_placeholders(text);
  if (text.find(char16_t('&')) == ustring::npos) return text;
  ustring out;
  const std::size_t n = text.size();
  std::size_t i = 0;
  while (i < n) {
    if (text[i] != '&') {
      out += text[i++];
      continue;
    }
    // &#123; &#x1F; &name;
    std::size_t j = i + 1;
    bool done = false;
    if (j < n && text[j] == '#') {
      std::size_t k = j + 1;
      while (k < n && is_digit(text[k])) k++;
      if (k > j + 1 && k < n && text[k] == ';') {
        out += code_point(number(ustring_view(text).substr(j + 1, k - j - 1), 10), ustring_view(text).substr(i, k + 1 - i));
        i = k + 1;
        done = true;
      } else if (j + 1 < n && (text[j + 1] == 'x' || text[j + 1] == 'X')) {
        k = j + 2;
        while (k < n && is_hex(text[k])) k++;
        if (k > j + 2 && k < n && text[k] == ';') {
          out += code_point(number(ustring_view(text).substr(j + 2, k - j - 2), 16), ustring_view(text).substr(i, k + 1 - i));
          i = k + 1;
          done = true;
        }
      }
    } else {
      std::size_t k = j;
      while (k < n && is_word(text[k])) k++;
      if (k > j && k < n && text[k] == ';') {
        ustring found;
        if (lookup(ustring_view(text).substr(j, k - j), found)) out += found;
        else out += text.substr(i, k + 1 - i);
        i = k + 1;
        done = true;
      }
    }
    if (!done) out += text[i++];
  }
  return out;
}

}  // namespace pele
