#include "cluster.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "js_math.h"
#include "svg.h"

namespace pele {

void crossing_map::mark(const std::vector<double>& route) {
  for (std::size_t k = 0; k + 1 < route.size(); k += 3) crossings_[static_cast<long long>(js_round(route[k + 1]))].push_back(route[k]);
}

double crossing_map::title_x(const compound_node& c, double width, double pad) const {
  if (width == 0) return c.x;
  const double top = c.y - c.h / 2;
  const double left = c.x - c.w / 2;
  const double half = width / 2;
  const double candidates[3] = {left + pad - 6 + half, c.x, left + c.w - pad + 6 - half};
  const auto it = crossings_.find(static_cast<long long>(js_round(top)));
  if (it == crossings_.end()) return candidates[0];
  double best = candidates[0];
  double best_gap = -1;
  for (double x : candidates) {
    double gap = std::numeric_limits<double>::infinity();
    for (double cx : it->second)
      if (cx > left && cx < left + c.w) gap = std::min(gap, std::abs(cx - x) - half);
    if (gap >= 6) return x;
    if (gap > best_gap + 0.5) {
      best_gap = gap;
      best = x;
    }
  }
  return best;
}

bool crossing_map::crossed(const compound_node& c, double width, double x) const {
  if (width <= 0) return false;
  const auto it = crossings_.find(static_cast<long long>(js_round(c.y - c.h / 2)));
  if (it == crossings_.end()) return false;
  const double left = c.x - c.w / 2;
  for (double cx : it->second)
    if (cx > left && cx < left + c.w && std::abs(cx - x) < width / 2 + 4) return true;
  return false;
}

std::string struck_title(const label& l, double x, double y, const std::string& attrs, const std::string& id, const std::vector<compound_node>& nodes,
                         std::size_t index, const icon_set* icons) {
  int depth = 1;
  for (int p = nodes[index].parent; p >= 0 && depth < 8; p = nodes[p].parent) depth++;
  // Each cluster is half its fill over what is behind it.
  const std::string fill = "color-mix(in srgb,var(--_a) " + svg::num(100 * (1 - std::pow(0.5, depth))) + "%,var(--_bg))";
  return "<g class=\"pele-cluster-title\" data-id=\"" + id + "\">" +
         svg::label_svg(l, x, y, " fill=\"none\" stroke=\"" + fill + "\" stroke-width=\"5\" stroke-linejoin=\"round\"", icons) + svg::label_svg(l, x, y, attrs, icons) +
         "</g>";
}

}  // namespace pele
