#include "ustring.h"

#include "chars.h"

namespace pele {

ustring to_utf16(std::string_view s) {
  ustring out;
  out.reserve(s.size());
  const std::size_t n = s.size();
  std::size_t i = 0;
  auto cont = [&](std::size_t k) { return k < n && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80; };
  while (i < n) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    unsigned long cp = 0xFFFD;
    std::size_t len = 1;
    if (c < 0x80) {
      cp = c;
    } else if (c >= 0xC2 && c <= 0xDF && cont(i + 1)) {
      cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
      len = 2;
    } else if (c >= 0xE0 && c <= 0xEF && cont(i + 1) && cont(i + 2)) {
      const unsigned long v = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
      if (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) {
        cp = v;
        len = 3;
      }
    } else if (c >= 0xF0 && c <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
      const unsigned long v = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                              ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
      if (v >= 0x10000 && v <= 0x10FFFF) {
        cp = v;
        len = 4;
      }
    }
    out += from_code_point(cp);
    i += len;
  }
  return out;
}

std::string to_utf8(ustring_view s) {
  std::string out;
  out.reserve(s.size());
  const std::size_t n = s.size();
  for (std::size_t i = 0; i < n; i++) {
    unsigned long cp = s[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      i++;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = 0xFFFD;
    }
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

ustring from_code_point(unsigned long cp) {
  ustring out;
  if (cp >= 0x10000) {
    cp -= 0x10000;
    out += static_cast<char16_t>(0xD800 + (cp >> 10));
    out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
  } else {
    out += static_cast<char16_t>(cp);
  }
  return out;
}

ustring trim_start(ustring_view s) {
  std::size_t i = 0;
  while (i < s.size() && is_space(s[i])) i++;
  return ustring(s.substr(i));
}

ustring trim_end(ustring_view s) {
  std::size_t n = s.size();
  while (n > 0 && is_space(s[n - 1])) n--;
  return ustring(s.substr(0, n));
}

ustring trim(ustring_view s) {
  std::size_t a = 0;
  std::size_t b = s.size();
  while (a < b && is_space(s[a])) a++;
  while (b > a && is_space(s[b - 1])) b--;
  return ustring(s.substr(a, b - a));
}

ustring ascii(std::string_view s) {
  ustring out;
  out.reserve(s.size());
  for (char c : s) out += static_cast<char16_t>(static_cast<unsigned char>(c));
  return out;
}

}  // namespace pele
