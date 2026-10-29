#pragma once

#include <charconv>
#include <cmath>
#include <string>

namespace pele {

// M_PI is not standard: MSVC only defines it with _USE_MATH_DEFINES.
namespace constant {
inline constexpr double pi = 3.14159265358979323846;
}  // namespace constant

// Math.round: halves go up.
inline double js_round(double v) { return std::floor(v + 0.5); }

// Math.sign: 0 for 0 and NaN for NaN. Callers that write `Math.sign(x) || 1` get 1 for both.
inline double js_sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : v; }

// Math.hypot as V8 computes it, so that the last bit is the same: scaled by the largest, summed with compensation.
inline double js_hypot(double a, double b) {
  const double x = std::abs(a);
  const double y = std::abs(b);
  if (std::isinf(x) || std::isinf(y)) return INFINITY;
  if (std::isnan(x) || std::isnan(y)) return NAN;
  const double max = x > y ? x : y;
  if (max == 0) return 0;
  double sum = 0;
  double compensation = 0;
  for (double v : {x, y}) {
    const double n = v / max;
    const double summand = n * n - compensation;
    const double preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }
  return std::sqrt(sum) * max;
}

// A number the way String(n) writes it (ECMAScript Number::toString): the shortest digits that read
// back as the same number, in fixed notation from 1e-6 up to 1e21 and in exponent notation outside.
inline std::string js_number_string(double v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";
  if (v == 0) return "0";
  const bool negative = v < 0;
  char buf[64];
  const auto r = std::to_chars(buf, buf + sizeof buf, negative ? -v : v, std::chars_format::scientific);
  const std::string sci(buf, r.ptr);  // d[.ddd]e[+-]XX
  const std::size_t e_at = sci.find('e');
  std::string digits;
  for (std::size_t i = 0; i < e_at; i++)
    if (sci[i] != '.') digits += sci[i];
  const int exponent = std::stoi(sci.substr(e_at + 1));
  const int k = static_cast<int>(digits.size());
  const int n = exponent + 1;
  std::string out;
  if (k <= n && n <= 21) {
    out = digits + std::string(static_cast<std::size_t>(n - k), '0');
  } else if (0 < n && n <= 21) {
    out = digits.substr(0, static_cast<std::size_t>(n)) + "." + digits.substr(static_cast<std::size_t>(n));
  } else if (-6 < n && n <= 0) {
    out = "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
  } else {
    const int e = n - 1;
    out = digits.substr(0, 1) + (k > 1 ? "." + digits.substr(1) : "") + "e" + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
  }
  return negative ? "-" + out : out;
}

// `x.toFixed(0)` for a number that is not negative. A value halfway between two integers goes up,
// where printf's "%.0f" would go to the even one.
inline std::string js_to_fixed0(double x) {
  if (std::isnan(x)) return "NaN";
  if (x >= 1e21) return js_number_string(x);
  const double whole = std::floor(x);
  return js_number_string(x - whole >= 0.5 ? whole + 1 : whole);
}

}  // namespace pele
