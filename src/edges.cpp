#include "edges.h"
#include "ink.h"

#include <algorithm>
#include <initializer_list>
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

  // Keep tokens to avoid parsing d when checking fills.
  std::vector<double> tokens;
  // Match the value parsed from num() output.
  auto written = [](double v) {
    const double n = js_round(v * 100);
    return n == 0 ? 0.0 : n / 100;
  };
  auto tok = [&](double v) {
    tokens.push_back(written(v));
    return num(v);
  };
  tokens.push_back(77);
  tokens.push_back(2);
  // The order tokens are recorded in must be the order they are written, so each call is its own statement.
  auto toks = [&](std::initializer_list<double> values) {
    std::vector<std::string> out;
    for (double v : values) out.push_back(tok(v));
    return out;
  };
  std::string d;
  {
    const auto m = toks({xs[0], ys[0]});
    d = "M" + m[0] + "," + m[1];
  }
  double at_x = xs[0];
  double at_y = ys[0];
  auto line_to = [&](double x, double y) {
    if (near_points(x, y, at_x, at_y)) return;
    tokens.push_back(76);
    tokens.push_back(2);
    const auto p = toks({x, y});
    d += "L" + p[0] + "," + p[1];
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
      tokens.push_back(67);
      tokens.push_back(6);
      const auto p = toks({at_x + ux, at_y + uy, x - ux, y - uy, x, y});
      d += "C" + p[0] + "," + p[1] + " " + p[2] + "," + p[3] + " " + p[4] + "," + p[5];
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
    if (stepped) {
      tokens.push_back(81);
      tokens.push_back(4);
      const auto p = toks({x, y, at_x, at_y});
      d += "Q" + p[0] + "," + p[1] + " " + p[2] + "," + p[3];
    } else {
      tokens.push_back(67);
      tokens.push_back(6);
      const auto p = toks({x - ux * before * (1 - kHandle), y - uy * before * (1 - kHandle), x + vx * after * (1 - kHandle), y + vy * after * (1 - kHandle), at_x, at_y});
      d += "C" + p[0] + "," + p[1] + " " + p[2] + "," + p[3] + " " + p[4] + "," + p[5];
    }
  }
  edge_path out;
  out.d = std::move(d);
  out.tokens = std::move(tokens);
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

std::string edge_geometry::marker(std::string_view type, double x, double y, double dx, double dy, std::string_view color, std::string_view stroke_view) {
  const double nx = -dy;
  const double ny = dx;
  const std::string c(color);
  const std::string stroke(stroke_view);
  if (type == "arrow_point") {
    const double bx = x - dx * kArrow;
    const double by = y - dy * kArrow;
    const double half = kArrow * 0.42;
    return "<path class=\"pele-marker\" d=\"M" + num(x) + "," + num(y) + "L" + num(bx + nx * half) + "," + num(by + ny * half) + "L" + num(bx - nx * half) +
           "," + num(by - ny * half) + "Z\" fill=\"" + c + "\" stroke=\"" + stroke + "\"/>";
  }
  if (type == "arrow_circle")
    return "<circle class=\"pele-marker\" cx=\"" + num(x - dx * 4) + "\" cy=\"" + num(y - dy * 4) + "\" r=\"3.5\" fill=\"" + c + "\" stroke=\"" + stroke + "\"/>";
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

namespace {

double jmin(double a, double b) { return std::isnan(a) || std::isnan(b) ? std::nan("") : std::min(a, b); }
double jmax(double a, double b) { return std::isnan(a) || std::isnan(b) ? std::nan("") : std::max(a, b); }

// Each command stores its character code, argument count, and arguments.
std::vector<double> tokenize(const std::string& d) {
  std::vector<double> out;
  long long at = -1;
  const std::size_t n = d.size();
  for (std::size_t i = 0; i < n;) {
    const unsigned char c = static_cast<unsigned char>(d[i]);
    if (c >= 65 && c <= 90 && c != 69) {
      at = static_cast<long long>(out.size()) + 1;
      out.push_back(c);
      out.push_back(0);
      i++;
    } else if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
      std::size_t j = c == '-' ? i + 1 : i;
      bool dot = false;
      while (j < n && ((d[j] >= '0' && d[j] <= '9') || (d[j] == '.' && !dot))) {
        if (d[j] == '.') dot = true;
        j++;
      }
      if (j < n && (d[j] | 32) == 'e') {
        std::size_t k = j + 1;
        if (k < n && (d[k] == '-' || d[k] == '+')) k++;
        std::size_t digits = k;
        while (k < n && d[k] >= '0' && d[k] <= '9') k++;
        if (k > digits) j = k;
      }
      const double v = std::strtod(d.substr(i, j - i).c_str(), nullptr);
      if (at >= 0) {
        out.push_back(v);
        out[static_cast<std::size_t>(at)]++;
      }
      i = j > i ? j : i + 1;
    } else {
      i++;
    }
  }
  return out;
}

// Samples an unrotated arc from (x1, y1), after SVG's conversion from end points to a center.
void arc(std::vector<double>& out, double x1, double y1, double rx, double ry, double large, double sweep, double x2, double y2) {
  const double hx = (x1 - x2) / 2;
  const double hy = (y1 - y2) / 2;
  if (rx == 0 || ry == 0 || (hx == 0 && hy == 0)) {
    out.push_back(x2);
    out.push_back(y2);
    return;
  }
  const double scale = std::max(1.0, std::sqrt((hx * hx) / (rx * rx) + (hy * hy) / (ry * ry)));
  rx *= scale;
  ry *= scale;
  const double k = (large == sweep ? -1 : 1) * std::sqrt(std::max(0.0, (rx * rx * ry * ry - rx * rx * hy * hy - ry * ry * hx * hx) / (rx * rx * hy * hy + ry * ry * hx * hx)));
  const double cx = (k * rx * hy) / ry + (x1 + x2) / 2;
  const double cy = (-k * ry * hx) / rx + (y1 + y2) / 2;
  const double from = std::atan2((y1 - cy) / ry, (x1 - cx) / rx);
  double turn = std::atan2((y2 - cy) / ry, (x2 - cx) / rx) - from;
  if (sweep != 0 && turn < 0) turn += 2 * constant::pi;
  else if (sweep == 0 && turn > 0) turn -= 2 * constant::pi;
  for (int i = 1; i <= 8; i++) {
    out.push_back(cx + rx * std::cos(from + (turn * i) / 8));
    out.push_back(cy + ry * std::sin(from + (turn * i) / 8));
  }
}

// Samples curves and arcs; NaN separates subpaths.
std::vector<double> sample(const std::vector<double>& tokens) {
  std::vector<double> out;
  double x = 0;
  double y = 0;
  for (std::size_t i = 0; i < tokens.size();) {
    const int cmd = static_cast<int>(tokens[i]);
    const std::size_t count = static_cast<std::size_t>(tokens[i + 1]);
    const std::size_t s = i + 2;
    const std::size_t e = s + count;
    i = e;
    if (cmd == 72 || cmd == 86) {
      for (std::size_t k = s; k < e; k++) {
        if (cmd == 72) x = tokens[k];
        else y = tokens[k];
        out.push_back(x);
        out.push_back(y);
      }
    } else if (cmd == 67 || cmd == 81) {
      const bool cubic = cmd == 67;
      const std::size_t step = cubic ? 6 : 4;
      for (std::size_t k = s; k + step <= e; k += step) {
        const double x1 = tokens[k];
        const double y1 = tokens[k + 1];
        const double x2 = tokens[k + 2];
        const double y2 = tokens[k + 3];
        const double x3 = cubic ? tokens[k + 4] : x2;
        const double y3 = cubic ? tokens[k + 5] : y2;
        for (double t = 0.125; t <= 1; t += 0.125) {
          // Preserve the original De Casteljau operation order to keep rounding unchanged.
          double ax = x + (x1 - x) * t;
          double ay = y + (y1 - y) * t;
          double bx = x1 + (x2 - x1) * t;
          double by = y1 + (y2 - y1) * t;
          if (cubic) {
            const double cx = x2 + (x3 - x2) * t;
            const double cy = y2 + (y3 - y2) * t;
            ax += (bx - ax) * t;
            ay += (by - ay) * t;
            bx += (cx - bx) * t;
            by += (cy - by) * t;
          }
          out.push_back(ax + (bx - ax) * t);
          out.push_back(ay + (by - ay) * t);
        }
        x = x3;
        y = y3;
      }
    } else if (cmd == 65) {
      for (std::size_t k = s; k + 7 <= e; k += 7) {
        arc(out, x, y, tokens[k], tokens[k + 1], tokens[k + 3], tokens[k + 4], tokens[k + 5], tokens[k + 6]);
        x = tokens[k + 5];
        y = tokens[k + 6];
      }
    } else {
      for (std::size_t k = s; k + 2 <= e; k += 2) {
        if (cmd == 77 && k == s && !out.empty()) {
          out.push_back(std::nan(""));
          out.push_back(std::nan(""));
        }
        x = tokens[k];
        y = tokens[k + 1];
        out.push_back(x);
        out.push_back(y);
      }
    }
  }
  return out;
}

// Test segment overlap with the box using Liang-Barsky clipping.
bool clips(double x, double y, double dx, double dy, double l, double t, double r, double b) {
  double lo = 0;
  double hi = 1;
  for (int side = 0; side < 4; side++) {
    const double p = side == 0 ? -dx : side == 1 ? dx : side == 2 ? -dy : dy;
    const double q = side == 0 ? x - l : side == 1 ? r - x : side == 2 ? y - t : b - y;
    if (p == 0) {
      if (q < 0) hi = -1;
    } else if (p < 0) {
      lo = jmax(lo, q / p);
    } else {
      hi = jmin(hi, q / p);
    }
  }
  return lo < hi;
}

bool enters(const std::vector<double>& points, double l, double t, double r, double b) {
  for (std::size_t k = 0; k + 2 < points.size(); k += 2) {
    const double x = points[k];
    const double y = points[k + 1];
    const double x2 = points[k + 2];
    const double y2 = points[k + 3];
    if ((x < l && x2 < l) || (x > r && x2 > r) || (y < t && y2 < t) || (y > b && y2 > b)) continue;
    if (clips(x, y, x2 - x, y2 - y, l, t, r, b)) return true;
  }
  return false;
}

// The value of `[";]prop:([^;"]+);` in a line style, trimmed.
std::optional<std::string> decl(const std::string& line, const std::string& prop) {
  if (line.find(prop) == std::string::npos) return std::nullopt;
  const std::string key = prop + ":";
  for (std::size_t i = 0; i + 1 + key.size() <= line.size(); i++) {
    if ((line[i] != '"' && line[i] != ';') || line.compare(i + 1, key.size(), key) != 0) continue;
    const std::size_t from = i + 1 + key.size();
    std::size_t end = from;
    while (end < line.size() && line[end] != ';' && line[end] != '"') end++;
    if (end > from && end < line.size() && line[end] == ';') return to_utf8(trim(to_utf16(line.substr(from, end - from))));
  }
  return std::nullopt;
}

bool plain_length(const std::string& w) {
  // /^[\d.]+(?:px)?$/
  std::size_t i = 0;
  while (i < w.size() && ((w[i] >= '0' && w[i] <= '9') || w[i] == '.')) i++;
  if (i == 0) return false;
  return i == w.size() || w.compare(i, std::string::npos, "px") == 0;
}

constexpr double kCase = 4;

}  // namespace

std::optional<std::string> edge_geometry::over_fills(const std::string& d, const std::vector<double>& tips, const std::vector<fill>& fills, const std::string& line,
                                                     const std::vector<double>& tokens) {
  if (fills.empty()) return std::nullopt;
  const std::vector<double> points = !tokens.empty() ? sample(tokens) : sample(tokenize(d));
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = min_x;
  double max_x = -min_x;
  double max_y = -min_x;
  for (std::size_t k = 0; k < points.size() + tips.size(); k += 2) {
    const double x = k < points.size() ? points[k] : tips[k - points.size()];
    const double y = k < points.size() ? points[k + 1] : tips[k + 1 - points.size()];
    if (x < min_x) min_x = x;
    if (x > max_x) max_x = x;
    if (y < min_y) min_y = y;
    if (y > max_y) max_y = y;
  }
  const fill* holder = nullptr;
  double least = std::numeric_limits<double>::infinity();
  for (const fill& f : fills) {
    const double l = f.box[0];
    const double t = f.box[1];
    const double r = f.box[2];
    const double b = f.box[3];
    if (max_x < l + 0.5 || min_x > r - 0.5 || max_y < t + 0.5 || min_y > b - 0.5) continue;
    bool touched = enters(points, l + 0.5, t + 0.5, r - 0.5, b - 0.5);
    for (std::size_t k = 0; k < tips.size() && !touched; k += 2) touched = tips[k] > l + 0.5 && tips[k] < r - 0.5 && tips[k + 1] > t + 0.5 && tips[k + 1] < b - 0.5;
    if (!touched) continue;
    if (min_x < l || max_x > r || min_y < t || max_y > b) return std::string();
    const double area = (r - l) * (b - t);
    if (area < least) {
      least = area;
      holder = &f;
    }
  }
  if (holder == nullptr) return std::nullopt;
  // The fill's ink for an edge on it, or "" for a casing when unknown or when the line style sets a stroke.
  if (line.find("\"stroke:") != std::string::npos || line.find(";stroke:") != std::string::npos) return std::string();
  const auto color = ink(holder->shape);
  return color ? *color : std::string();
}

std::string edge_geometry::edge_casing(const edge_path& path, std::string_view start_type, std::string_view end_type, const std::string& line, bool thick) {
  const std::string markers = marker(start_type, path.sx, path.sy, path.sdx, path.sdy, "var(--_bg)", "var(--_bg)") +
                              marker(end_type, path.ex, path.ey, path.edx, path.edy, "var(--_bg)", "var(--_bg)");
  const auto stroke = decl(line, "stroke");
  if (stroke && (*stroke == "none" || *stroke == "transparent")) return "";
  const std::optional<std::string> width = thick ? std::optional<std::string>("2.5") : decl(line, "stroke-width");
  std::string attrs;
  std::string style;
  if (width && plain_length(*width)) {
    const double w = std::strtod(width->c_str(), nullptr);
    if (w != 1) attrs = " stroke-width=\"" + num(w + kCase) + "\"";
  } else if (width) {
    style += "stroke-width:calc(" + *width + " + " + js_number_string(kCase) + "px);";
  }
  for (const char* prop : {"opacity", "stroke-opacity"}) {
    const auto value = decl(line, prop);
    if (value) style += std::string(prop) + ":" + *value + ";";
  }
  std::string cleaned;
  const std::string drop = " class=\"pele-marker\"";
  for (std::size_t i = 0; i < markers.size();) {
    if (markers.compare(i, drop.size(), drop) == 0) i += drop.size();
    else cleaned += markers[i++];
  }
  const std::string body = "<path d=\"" + path.d + "\"/>" + cleaned;
  return !attrs.empty() || !style.empty() ? "<g" + attrs + (!style.empty() ? " style=\"" + style + "\"" : "") + ">" + body + "</g>" : body;
}

std::string edge_geometry::casings_svg(const std::string& casings) {
  return casings.empty() ? ""
                         : "<g class=\"pele-casings\" fill=\"none\" stroke=\"var(--_bg)\" stroke-width=\"5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" pointer-events=\"none\">" +
                               casings + "</g>";
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
