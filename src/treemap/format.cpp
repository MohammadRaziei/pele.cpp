#include "treemap/format.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "js_format.h"
#include "js_math.h"
#include "regex.h"

namespace pele::treemap {
namespace {

const js_regex& re_spec() {
  static const js_regex re(R"(^(?:.?[<>=^])?([-+( ])?(\$)?0?\d*(,)?(?:\.(\d+))?(~)?([a-zA-Z%])?$)");
  return re;
}

std::string group(const std::string& digits) {
  std::string out;
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(digits.size()); i > 0; i -= 3) {
    const std::size_t from = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, i - 3));
    out = digits.substr(from, static_cast<std::size_t>(i) - from) + (!out.empty() ? "," + out : std::string());
  }
  return out;
}

}  // namespace

value_formatter::value_formatter(ustring_view spec) {
  // Mermaid documents `$0,0` for a dollar amount with thousands separators.
  ustring text(spec == u"$0,0" ? ustring_view(u"$,") : spec);
  auto m = re_spec().search(text);
  if (!m) {
    text = u",";
    m = re_spec().search(text);
  }
  plus_ = m->has(1) && m->group(text, 1) == u"+";
  prefix_ = m->has(2) ? to_utf8(m->group(text, 2)) : std::string();
  comma_ = m->has(3);
  trim_ = m->has(5);
  if (m->has(4)) {
    has_precision_ = true;
    // `Math.min(Number(digits), 20)`
    precision_ = static_cast<int>(std::min(std::strtod(to_utf8(m->group(text, 4)).c_str(), nullptr), 20.0));
  }
  type_ = m->has(6) ? to_utf8(m->group(text, 6)) : std::string();
}

std::string value_formatter::operator()(double value) const {
  const double abs = std::abs(value);
  std::string body;
  std::string suffix;
  if (type_ == "f") {
    body = js_to_fixed(abs, has_precision_ ? precision_ : 6);
  } else if (type_ == "%") {
    body = js_to_fixed(abs * 100, has_precision_ ? precision_ : 6);
    suffix = "%";
  } else if (type_ == "d") {
    body = js_to_fixed(js_round(abs), 0);
  } else if (type_ == "e") {
    body = js_to_exponential(abs, has_precision_ ? precision_ : 6);
  } else {
    body = js_number_string(std::strtod(js_to_precision(abs, std::max(1, has_precision_ ? precision_ : 12)).c_str(), nullptr));
  }

  if (trim_ && body.find('.') != std::string::npos && body.find('e') == std::string::npos) {
    // `body.replace(/\.?0+$/, '')`
    static const js_regex trailing(R"(\.?0+$)");
    body = to_utf8(trailing.replace_all(to_utf16(body), ustring_view(u"")));
  }
  if (comma_ && body.find('e') == std::string::npos) {
    const std::size_t dot = body.find('.');
    body = dot == std::string::npos ? group(body) : group(body.substr(0, dot)) + body.substr(dot);
  }
  return std::string(value < 0 ? "-" : plus_ ? "+" : "") + prefix_ + body + suffix;
}

}  // namespace pele::treemap
