#include "edges.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "js_math.h"
#include "layered.h"
#include "svg.h"

namespace pele {
namespace {

// The most a corner is rounded by in a stepped route.
constexpr double kCorner = 12;
// How far the handles of a wide turn reach along its two legs. A little more than a circle's,
// which makes the turn fuller.
constexpr double kHandle = 0.62;

using svg::num;

bool near_points(double ax, double ay, double bx, double by) { return std::abs(ax - bx) < 0.01 && std::abs(ay - by) < 0.01; }

}  // namespace

void side_reach::add(int node, int side, std::string_view marker_type) {
  const double trim = edge_geometry::marker_trim(marker_type);
  if (side < 0 || trim <= 0) return;
  const long long key = static_cast<long long>(node) * 4 + side;
  auto it = reach_.find(key);
  const double current = it == reach_.end() ? 0 : it->second;
  if (trim + edge_geometry::kLead > current) reach_[key] = trim + edge_geometry::kLead;
}

double side_reach::of(int node, int side) const {
  if (side < 0) return 0;
  auto it = reach_.find(static_cast<long long>(node) * 4 + side);
  return it == reach_.end() ? 0 : it->second;
}

edge_path edge_geometry::route_path(const std::vector<double>& route, std::string_view curve, double start_trim, double end_trim,
                                    std::optional<double> start_reach_opt, std::optional<double> end_reach_opt) {
  const double start_reach = start_reach_opt ? *start_reach_opt : (start_trim > 0 ? start_trim + kLead : 0);
  const double end_reach = end_reach_opt ? *end_reach_opt : (end_trim > 0 ? end_trim + kLead : 0);
  const bool linear = curve == "linear";
  const bool stepped = curve == "step" || curve == "stepBefore" || curve == "stepAfter";
  std::vector<double> xs;
  std::vector<double> ys;
  std::vector<double> axes;
  for (std::size_t i = 0; i + 3 <= route.size(); i += 3) {
    const double x = route[i];
    const double y = route[i + 1];
    const double axis = route[i + 2];
    const long last_index = static_cast<long>(xs.size()) - 1;
    // A stepped route turns at right angles where a smooth one would curve.
    if (stepped && last_index >= 0 && std::abs(x - xs[last_index]) >= 0.01 && std::abs(y - ys[last_index]) >= 0.01) {
      const double px = xs[last_index];
      const double py = ys[last_index];
      const double my = curve == "stepBefore" ? py : curve == "stepAfter" ? y : (py + y) / 2;
      const double mx = curve == "stepBefore" ? px : curve == "stepAfter" ? x : (px + x) / 2;
      const double turns_at[4] = {axis == 0 ? px : mx, axis == 0 ? my : py, axis == 0 ? x : mx, axis == 0 ? my : y};
      for (int k = 0; k < 4; k += 2) {
        if (near_points(turns_at[k], turns_at[k + 1], px, py) || near_points(turns_at[k], turns_at[k + 1], x, y)) continue;
        xs.push_back(turns_at[k]);
        ys.push_back(turns_at[k + 1]);
        axes.push_back(axis);
      }
    }
    xs.push_back(x);
    ys.push_back(y);
    axes.push_back(axis);
  }
  int last = static_cast<int>(xs.size()) - 1;
  auto aligned = [&](int i) { return std::abs(xs[i] - xs[i - 1]) < 0.01 || std::abs(ys[i] - ys[i - 1]) < 0.01; };
  auto straight = [&](int i) { return linear || aligned(i); };
  // The way the route heads from point i to point j: along the flow where an S joins them.
  auto unit = [&](int i, int j, double axis) -> std::pair<double, double> {
    if (!straight(std::max(i, j))) {
      if (axis == 0) {
        const double s = js_sign(ys[j] - ys[i]);
        return {0, s != 0 ? s : 1};
      }
      const double s = js_sign(xs[j] - xs[i]);
      return {s != 0 ? s : 1, 0};
    }
    const double dx = xs[j] - xs[i];
    const double dy = ys[j] - ys[i];
    const double h = js_hypot(dx, dy);
    const double len = h != 0 ? h : 1;
    return {dx / len, dy / len};
  };
  const auto [sdx, sdy] = unit(0, 1, axes[1]);
  const auto [edx, edy] = unit(last - 1, last, axes[last]);
  const double sx = xs[0];
  const double sy = ys[0];
  const double ex = xs[last];
  const double ey = ys[last];
  xs[0] += sdx * start_trim;
  ys[0] += sdy * start_trim;
  xs[last] -= edx * end_trim;
  ys[last] -= edy * end_trim;
  auto length = [&](int i) { return std::abs(xs[i] - xs[i - 1]) + std::abs(ys[i] - ys[i - 1]); };
  // Whether the route turns a corner at point i, from running one way to running the other.
  auto turns = [&](int i) -> bool {
    return !linear && i > 0 && i < last && aligned(i) && aligned(i + 1) && length(i) >= 0.01 && length(i + 1) >= 0.01 &&
           (std::abs(xs[i] - xs[i - 1]) < 0.01) != (std::abs(xs[i + 1] - xs[i]) < 0.01);
  };
  // An edge that curves into a marker runs straight for a little way first, where it has the
  // room, so that the marker does not sit on the bend.
  auto along_of = [&](int i, double ux, double uy) { return std::abs(xs[i] - xs[i - 1]) * std::abs(ux) + std::abs(ys[i] - ys[i - 1]) * std::abs(uy); };
  // Of the way from the end of a run to where it turns, the part that makes up for a larger
  // marker beside this one is kept where it can be, and the rest is at most a quarter of what is
  // left, so that ends on one side of a node turn equally far from it.
  auto lead = [&](double reach, double trim, double run) {
    const double room = std::min(std::max(0.0, reach - kLead - trim), run / 4);
    return room + std::min(reach - trim - room, (run - room) / 4);
  };
  const double arrives = end_reach > end_trim && (!straight(last) || turns(last - 1)) ? lead(end_reach, end_trim, along_of(last, edx, edy)) : 0;
  const double leaves = start_reach > start_trim && (!straight(1) || turns(1)) ? lead(start_reach, start_trim, along_of(1, sdx, sdy)) : 0;
  if (arrives >= 1) {
    const double nx = xs[last] - edx * arrives;
    const double ny = ys[last] - edy * arrives;
    const double na = axes[last];
    xs.insert(xs.begin() + last, nx);
    ys.insert(ys.begin() + last, ny);
    axes.insert(axes.begin() + last, na);
    last++;
  }
  if (leaves >= 1) {
    const double nx = xs[0] + sdx * leaves;
    const double ny = ys[0] + sdy * leaves;
    const double na = axes[1];
    xs.insert(xs.begin() + 1, nx);
    ys.insert(ys.begin() + 1, ny);
    axes.insert(axes.begin() + 1, na);
    last++;
  }

  std::string d = "M" + num(xs[0]) + "," + num(ys[0]);
  double at_x = xs[0];
  double at_y = ys[0];
  auto line_to = [&](double x, double y) {
    if (near_points(x, y, at_x, at_y)) return;
    d += "L" + num(x) + "," + num(y);
    at_x = x;
    at_y = y;
  };
  for (int i = 1; i <= last; i++) {
    const double x = xs[i];
    const double y = ys[i];
    if (!straight(i)) {
      const bool down = axes[i] == 0;
      const double along = down ? y - at_y : x - at_x;
      const double k = std::abs(along) * layered_layouter::ease(down ? x - at_x : y - at_y, along);
      const double ux = down ? 0 : js_sign(along) * k;
      const double uy = down ? js_sign(along) * k : 0;
      d += "C" + num(at_x + ux) + "," + num(at_y + uy) + " " + num(x - ux) + "," + num(y - uy) + " " + num(x) + "," + num(y);
      at_x = x;
      at_y = y;
      continue;
    }
    if (!turns(i)) {
      // A point part of the way along one straight run adds nothing to the path.
      if (i < last && aligned(i) && aligned(i + 1) && (x - xs[i - 1]) * (xs[i + 1] - x) + (y - ys[i - 1]) * (ys[i + 1] - y) > 0) continue;
      line_to(x, y);
      continue;
    }
    // How much of the leg before the turn and of the leg after it the turn takes: all of one that
    // ends at a node, short of the straight run into a marker, and half of one it shares.
    double before = turns(i - 1) ? length(i) / 2 : length(i);
    double after = turns(i + 1) ? length(i + 1) / 2 : length(i + 1);
    if (stepped) before = after = std::min(kCorner, std::min(before, after));
    const auto [ux, uy] = unit(i - 1, i, axes[i]);
    const auto [vx, vy] = unit(i, i + 1, axes[i + 1]);
    line_to(x - ux * before, y - uy * before);
    at_x = x + vx * after;
    at_y = y + vy * after;
    if (stepped) d += "Q" + num(x) + "," + num(y) + " " + num(at_x) + "," + num(at_y);
    else
      d += "C" + num(x - ux * before * (1 - kHandle)) + "," + num(y - uy * before * (1 - kHandle)) + " " + num(x + vx * after * (1 - kHandle)) + "," +
           num(y + vy * after * (1 - kHandle)) + " " + num(at_x) + "," + num(at_y);
  }
  edge_path out;
  out.d = std::move(d);
  out.sx = sx;
  out.sy = sy;
  out.sdx = -sdx;
  out.sdy = -sdy;
  out.ex = ex;
  out.ey = ey;
  out.edx = edx;
  out.edy = edy;
  return out;
}

double edge_geometry::marker_trim(std::string_view type) {
  return type == "arrow_point" ? kArrow - 1 : type == "arrow_circle" ? kArrow : type == "arrow_cross" ? 4 : 0;
}

std::string edge_geometry::marker(std::string_view type, double x, double y, double dx, double dy, std::string_view color) {
  const double nx = -dy;
  const double ny = dx;
  const std::string c(color);
  if (type == "arrow_point") {
    const double bx = x - dx * kArrow;
    const double by = y - dy * kArrow;
    const double half = kArrow * 0.42;
    return "<path class=\"pele-marker\" d=\"M" + num(x) + "," + num(y) + "L" + num(bx + nx * half) + "," + num(by + ny * half) + "L" + num(bx - nx * half) +
           "," + num(by - ny * half) + "Z\" fill=\"" + c + "\" stroke=\"none\"/>";
  }
  if (type == "arrow_circle")
    return "<circle class=\"pele-marker\" cx=\"" + num(x - dx * 4) + "\" cy=\"" + num(y - dy * 4) + "\" r=\"3.5\" fill=\"" + c + "\" stroke=\"none\"/>";
  if (type == "arrow_cross") {
    const double cx = x - dx * 4;
    const double cy = y - dy * 4;
    const double a = 3.5;
    const double ux = (dx + nx) * a;
    const double uy = (dy + ny) * a;
    const double vx = (dx - nx) * a;
    const double vy = (dy - ny) * a;
    return "<path class=\"pele-marker\" d=\"M" + num(cx - ux) + "," + num(cy - uy) + "L" + num(cx + ux) + "," + num(cy + uy) + "M" + num(cx - vx) + "," +
           num(cy - vy) + "L" + num(cx + vx) + "," + num(cy + vy) + "\"/>";
  }
  return "";
}

std::string edge_geometry::label_svg(std::string_view id, const label& l, double x, double y, const std::string& attrs, const icon_set* icons) {
  const double w = l.width + 8;
  const double h = l.height;
  return "<g class=\"pele-edge-label\" data-id=\"" + svg::esc_text(id) + "\"><rect x=\"" + num(x - w / 2) + "\" y=\"" + num(y - h / 2) + "\" width=\"" +
         num(w) + "\" height=\"" + num(h) + "\" rx=\"3\" fill=\"var(--_bg)\"/>" + svg::label_svg(l, x, y, attrs, icons) + "</g>";
}

edge_path edge_geometry::loop_path(double cx, double cy, double w, double h, int k, bool sideways, double base, double trim, loop_label& e) {
  const double half = (sideways ? w : h) / 2;
  const double spread = std::min(half - 4, 8.0 + k * 6);
  const double reach = base + k * 8;
  const double hy = js_hypot(reach, spread);
  const double len = hy != 0 ? hy : 1;
  // Local frame: `out` points away from the node, `along` runs along its side.
  auto at = [&](double out, double along) -> std::string {
    return sideways ? num(cx + along) + "," + num(cy + h / 2 + out) : num(cx + w / 2 + out) + "," + num(cy + along);
  };
  const double tx = (reach / len) * trim;
  const double ty = (spread / len) * trim;
  if (sideways) {
    e.label_x = cx;
    e.label_y = cy + h / 2 + reach + 4 + e.label_h / 2;
  } else {
    e.label_x = cx + w / 2 + reach + 4 + e.label_w / 2;
    e.label_y = cy;
  }
  edge_path out;
  out.d = "M" + at(0, -spread) + "C" + at(reach, -spread * 2) + " " + at(reach, spread * 2) + " " + at(tx, spread + ty);
  out.sx = sideways ? cx - spread : cx + w / 2;
  out.sy = sideways ? cy + h / 2 : cy - spread;
  out.sdx = sideways ? 0 : -1;
  out.sdy = sideways ? -1 : 0;
  out.ex = sideways ? cx + spread : cx + w / 2;
  out.ey = sideways ? cy + h / 2 : cy + spread;
  out.edx = sideways ? -spread / len : -reach / len;
  out.edy = sideways ? -reach / len : -spread / len;
  return out;
}

}  // namespace pele
