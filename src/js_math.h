#pragma once

#include <charconv>
#include <cmath>
#include <string>

namespace pele {

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

// A number the way String(n) writes it, for the sizes that JavaScript prints in a template string.
inline std::string js_number_string(double v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";
  if (v == 0) return "0";
  char buf[512];
  const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
  return std::string(buf, r.ptr);
}

}  // namespace pele
