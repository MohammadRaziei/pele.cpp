#include "common/literal_label.h"

#include <algorithm>
#include <cmath>

#include "chars.h"
#include "common/code_points.h"
#include "js_math.h"

namespace pele {
namespace {

// `/\s/.test(char)` for one code point.
bool is_space_cp(const ustring& c) { return c.size() == 1 && is_space(c[0]); }

}  // namespace

label literal_label(ustring_view text, const text_measurer& measurer, double size, double room, int style) {
  std::vector<ustring> lines;
  std::size_t line_start = 0;
  for (std::size_t line_end = 0; line_end <= text.size(); line_end++) {
    if (line_end != text.size() && text[line_end] != u'\n') continue;
    const std::vector<ustring> chars = code_points(text.substr(line_start, line_end - line_start));
    line_start = line_end + 1;
    if (chars.empty()) lines.push_back(ustring());
    std::size_t from = 0;
    const std::size_t count = chars.size();
    while (from < count) {
      const auto fits = [&](std::size_t length) { return measurer.width(join_slice(chars, from, from + length), size, style) <= room; };
      std::size_t hi = 1;
      while (hi < count - from && fits(hi)) hi = std::min(count - from, hi * 2);
      std::size_t lo = 1;
      while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (fits(mid)) lo = mid;
        else hi = mid - 1;
      }
      std::size_t take = lo;
      const bool wraps = from + take < count;
      if (wraps && !is_space_cp(chars[from + take])) {
        for (std::size_t i = take - 1; i > 0; i--) {
          if (is_space_cp(chars[from + i])) {
            take = i + 1;
            break;
          }
        }
      }
      ustring line = join_slice(chars, from, from + take);
      if (wraps) {
        ustring trimmed = trim_end(line);
        if (!trimmed.empty()) line = std::move(trimmed);
      }
      lines.push_back(std::move(line));
      from += take;
      while (wraps && from < count && is_space_cp(chars[from])) from++;
    }
  }
  if (text.empty()) lines.clear();

  label out;
  out.size = size;
  out.line_height = js_round(size * 1.5);
  for (const ustring& line : lines) {
    const double w = measurer.width(line, size, style);
    out.widths.push_back(w);
    span s;
    s.text = line;
    s.style = style;
    s.width = w;
    out.lines.push_back({s});
    out.width = std::max(out.width, w);
  }
  out.height = static_cast<double>(lines.size()) * out.line_height;
  return out;
}

}  // namespace pele
