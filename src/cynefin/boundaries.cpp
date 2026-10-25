#include "cynefin/boundaries.h"

#include <cmath>

#include "svg.h"

namespace pele::cynefin {
namespace {

// `value | 0`: the integer part of a number, wrapped to 32 bits, with non-numbers as 0.
std::int32_t to_int32(double v) {
  if (!std::isfinite(v)) return 0;
  const double whole = std::trunc(v);
  const double wrapped = std::fmod(whole, 4294967296.0);
  const double positive = wrapped < 0 ? wrapped + 4294967296.0 : wrapped;
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(positive));
}

// `Math.imul`
std::int32_t imul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }

std::int32_t wrap(std::int64_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v))); }

std::uint32_t shr(std::int32_t v, int n) { return static_cast<std::uint32_t>(v) >> n; }

}  // namespace

double seeded_random(double seed) {
  std::int32_t t = to_int32(seed + 1831565813.0);  // 0x6d2b79f5
  t = imul(t ^ static_cast<std::int32_t>(shr(t, 15)), t | 1);
  t ^= wrap(static_cast<std::int64_t>(t) + imul(t ^ static_cast<std::int32_t>(shr(t, 7)), t | 61));
  return static_cast<double>(static_cast<std::uint32_t>(t ^ static_cast<std::int32_t>(shr(t, 14)))) / 4294967296.0;
}

std::int32_t hash_string(ustring_view text) {
  std::int32_t hash = 0;
  for (char16_t c : text) {
    const std::int32_t shifted = static_cast<std::int32_t>(static_cast<std::uint32_t>(hash) << 5);
    hash = wrap(static_cast<std::int64_t>(shifted) - hash + c);
  }
  return hash;
}

double resolve_seed(double configured, ustring_view id) {
  if (std::isfinite(configured) && configured != 0) return configured;
  return hash_string(id);
}

wave_options fold_options() { return {7, true, 17, 31, 7, false, false}; }
wave_options horizon_options() { return {7, false, 23, 37, 11, false, false}; }

std::vector<double> wave(double from, double to, double center, double seed, double amplitude, const wave_options& o) {
  const double step = (to - from) / o.segments;
  std::vector<double> along;
  std::vector<double> across;
  for (int i = 0; i <= o.segments; i++) {
    const bool pinned = (i == 0 && o.pin_start) || (i == o.segments && o.pin_end);
    along.push_back(from + i * step);
    across.push_back(pinned ? center : center + seeded_random(seed + i * o.point_step) * amplitude * 2 - amplitude);
  }
  std::vector<double> out = o.vertical ? std::vector<double>{across[0], along[0]} : std::vector<double>{along[0], across[0]};
  for (int i = 0; i < o.segments; i++) {
    const std::size_t k = static_cast<std::size_t>(i);
    const double mid = (along[k] + along[k + 1]) / 2;
    const double offset = amplitude * 1.5 * (i % 2 == 0 ? 1 : -1) * seeded_random(seed + i * o.control_step + o.control_shift);
    if (o.vertical) {
      for (double v : {across[k] + offset, mid, across[k + 1] - offset, mid, across[k + 1], along[k + 1]}) out.push_back(v);
    } else {
      for (double v : {mid, across[k] + offset, mid, across[k + 1] - offset, along[k + 1], across[k + 1]}) out.push_back(v);
    }
  }
  return out;
}

std::vector<double> cliff(double cx, double top, double bottom, double amplitude) {
  const double h = bottom - top;
  return {cx, top, cx + amplitude, top + h * 0.2, cx - amplitude * 1.5, top + h * 0.55, cx + amplitude * 0.5, top + h * 0.75, cx - amplitude, top + h * 0.85, cx + amplitude * 0.3, top + h * 0.95, cx, bottom};
}

std::string forward(const std::vector<double>& c) {
  std::string d;
  for (std::size_t i = 2; i < c.size(); i += 6)
    d += "C" + svg::num(c[i]) + "," + svg::num(c[i + 1]) + " " + svg::num(c[i + 2]) + "," + svg::num(c[i + 3]) + " " + svg::num(c[i + 4]) + "," + svg::num(c[i + 5]);
  return d;
}

std::string backward(const std::vector<double>& c) {
  std::string d;
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(c.size()) - 6; i >= 2; i -= 6) {
    const std::size_t k = static_cast<std::size_t>(i);
    d += "C" + svg::num(c[k + 2]) + "," + svg::num(c[k + 3]) + " " + svg::num(c[k]) + "," + svg::num(c[k + 1]) + " " + svg::num(c[k - 2]) + "," + svg::num(c[k - 1]);
  }
  return d;
}

std::string curve_path(const std::vector<double>& c) { return "M" + svg::num(c[0]) + "," + svg::num(c[1]) + forward(c); }

std::string ellipse_path(double cx, double cy, double rx, double ry) {
  const std::string r = svg::num(rx) + "," + svg::num(ry);
  return "M" + svg::num(cx - rx) + "," + svg::num(cy) + "A" + r + " 0 1,1 " + svg::num(cx + rx) + "," + svg::num(cy) + "A" + r + " 0 1,1 " + svg::num(cx - rx) + "," + svg::num(cy) + "Z";
}

}  // namespace pele::cynefin
