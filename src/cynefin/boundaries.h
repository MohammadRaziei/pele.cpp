#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ustring.h"

namespace pele::cynefin {

// The geometry of Mermaid's Cynefin boundaries: seeded wavy lines between the domains and the
// S-shaped cliff between Clear and Chaotic. Curves are flat lists of numbers: a start point, then
// six numbers (two control points and an end point) per cubic segment.

// mulberry32, one step. `seed` is any number, read as JavaScript reads it for `| 0`.
double seeded_random(double seed);

// `hash = (hash << 5) - hash + charCode; hash |= 0` over the code units, as an int32.
std::int32_t hash_string(ustring_view text);

// A configured seed other than zero wins; otherwise the seed comes from the diagram's id.
double resolve_seed(double configured, ustring_view id);

struct wave_options {
  int segments = 7;
  bool vertical = true;
  // Multipliers Mermaid uses to vary the seed per point and per control point.
  double point_step = 17;
  double control_step = 31;
  double control_shift = 7;
  bool pin_start = false;
  bool pin_end = false;
};

wave_options fold_options();
wave_options horizon_options();

// A wavy line along one axis from `from` to `to`, centered on `center` in the other axis.
// Points jitter by up to the amplitude; a pinned end stays on the center line.
std::vector<double> wave(double from, double to, double center, double seed, double amplitude, const wave_options& o);

// The cliff: a steep S from (cx, top) down to (cx, bottom).
std::vector<double> cliff(double cx, double top, double bottom, double amplitude);

// Path commands that continue from the curve's start point to its end.
std::string forward(const std::vector<double>& curve);
// Path commands that continue from the curve's end point back to its start.
std::string backward(const std::vector<double>& curve);
std::string curve_path(const std::vector<double>& curve);
std::string ellipse_path(double cx, double cy, double rx, double ry);

}  // namespace pele::cynefin
