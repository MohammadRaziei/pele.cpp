#include "compound.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <array>
#include <set>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "js_math.h"

namespace pele {

flow_dir parse_direction(std::string_view dir) {
  if (dir == "BT") return flow_dir::bt;
  if (dir == "LR") return flow_dir::lr;
  if (dir == "RL") return flow_dir::rl;
  return flow_dir::tb;
}

void compound_layouter::inherit_crossed_directions(std::vector<compound_node>& nodes, const std::vector<std::pair<int, int>>& ends) {
  auto ancestors = [&](int i) {
    std::set<int> set;
    for (int p = nodes[i].parent; p >= 0 && !set.count(p); p = nodes[p].parent) set.insert(p);
    return set;
  };
  for (const auto& end : ends) {
    const int src = end.first;
    const int dst = end.second;
    if (src < 0 || dst < 0 || src == dst) continue;
    const std::set<int> a = ancestors(src);
    const std::set<int> b = ancestors(dst);
    for (int g : a)
      if (!b.count(g)) nodes[g].has_dir = false;
    for (int g : b)
      if (!a.count(g)) nodes[g].has_dir = false;
  }
}

void compound_layouter::shift(std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, double ox, double oy) {
  for (compound_node& c : nodes) {
    c.x += ox;
    c.y += oy;
  }
  for (compound_edge& e : edges) {
    for (std::size_t k = 0; k + 1 < e.route.size(); k += 3) {
      e.route[k] += ox;
      e.route[k + 1] += oy;
    }
    e.label_x += ox;
    e.label_y += oy;
  }
}

namespace {

constexpr int kMaxPorts = 40000;

// Limit repeated layouts to small graphs.
constexpr int kMaxOrderedPorts = 400;
constexpr std::size_t kMaxOrderedEdges = 500;
// Crossing tolerance before trying independent group ordering.
constexpr double kFlatSlack = 1.5;
constexpr double kFlatSpare = 4;
// Ignore crossings within this distance of a shared node.
constexpr double kFan = 24;
// Tolerance for collinear points.
constexpr double kInLine = 0.5;
constexpr int kCurvePieces = 3;
constexpr int kCornerPieces = 2;
// Allow for a step, an S-curve, or a straight segment plus a rounded corner.
constexpr int kStepPieces = std::max(3, std::max(kCurvePieces, kCornerPieces + 1));
// Match the renderer's corner handles.
constexpr double kHandle = 0.62;
constexpr std::size_t kMaxInsert = 128;

enum class route_style { curved, stepped, linear };

route_style style_of(const layered_options& opt) { return opt.tracks ? route_style::stepped : opt.straight ? route_style::linear : route_style::curved; }

// How many times the routes cross, as drawn in the given style.
long long route_crossings(const std::vector<compound_node>& nodes, const std::vector<compound_edge>& edges, route_style style) {
  std::size_t size = 0;
  for (const compound_edge& e : edges)
    if (e.src != e.dst && e.route.size() > 3) size += (e.route.size() / 3 - 1) * kStepPieces;
  std::vector<double> ax(size), ay(size), bx(size), by(size), low(size), high(size);
  std::vector<int> owner(size, 0);
  // Endpoint flags: 1 for the first segment, 2 for the last.
  std::vector<std::uint8_t> ends(size, 0);
  std::size_t count = 0;
  double top = std::numeric_limits<double>::infinity();
  double bottom = -std::numeric_limits<double>::infinity();
  const double nan = std::nan("");
  double from_x = nan, from_y = nan, to_x = nan, to_y = nan;
  auto add = [&](int ei, int part, double x0, double y0, double x1, double y1) {
    const bool on = x0 == to_x && y0 == to_y && ((y1 == y0 && from_y == y0) || (x1 == x0 && from_x == x0));
    if (on && owner[count - 1] == ei && ends[count - 1] == part && (x0 - from_x) * (x1 - x0) + (y0 - from_y) * (y1 - y0) > 0) {
      count--;
      x0 = from_x;
      y0 = from_y;
    }
    from_x = x0;
    from_y = y0;
    to_x = x1;
    to_y = y1;
    ends[count] = static_cast<std::uint8_t>(part);
    ax[count] = std::min(x0, x1);
    bx[count] = std::max(x0, x1);
    ay[count] = x0 <= x1 ? y0 : y1;
    by[count] = x0 <= x1 ? y1 : y0;
    low[count] = std::min(y0, y1);
    high[count] = std::max(y0, y1);
    if (low[count] < top) top = low[count];
    if (high[count] > bottom) bottom = high[count];
    owner[count++] = ei;
  };
  for (int ei = 0; ei < static_cast<int>(edges.size()); ei++) {
    const compound_edge& e = edges[ei];
    if (e.src == e.dst) continue;
    const std::vector<double>& r = e.route;
    const long long rn = static_cast<long long>(r.size());
    auto aligned = [&](long long k) { return std::abs(r[k] - r[k - 3]) < 0.01 || std::abs(r[k + 1] - r[k - 2]) < 0.01; };
    auto length = [&](long long k) { return std::abs(r[k] - r[k - 3]) + std::abs(r[k + 1] - r[k - 2]); };
    auto turns = [&](long long k) {
      return style == route_style::curved && k > 0 && k < rn - 3 && aligned(k) && aligned(k + 3) && length(k) >= 0.01 && length(k + 3) >= 0.01 &&
             (std::abs(r[k] - r[k - 3]) < 0.01) != (std::abs(r[k + 3] - r[k]) < 0.01);
    };
    double at_x = r.empty() ? 0 : r[0];
    double at_y = r.empty() ? 0 : r[1];
    for (long long k = 3; k < rn; k += 3) {
      const int part = (k == 3 ? 1 : 0) | (k == rn - 3 ? 2 : 0);
      const double x0 = at_x;
      const double y0 = at_y;
      const double x1 = r[k];
      const double y1 = r[k + 1];
      at_x = x1;
      at_y = y1;
      if (aligned(k) || style == route_style::linear) {
        if (!turns(k)) {
          add(ei, part, x0, y0, x1, y1);
          continue;
        }
        // Adjacent corners share a leg, so each gets half.
        const double before = turns(k - 3) ? length(k) / 2 : length(k);
        const double after = turns(k + 3) ? length(k + 3) / 2 : length(k + 3);
        const double ux = (x1 - r[k - 3]) / length(k);
        const double uy = (y1 - r[k - 2]) / length(k);
        const double vx = (r[k + 3] - x1) / length(k + 3);
        const double vy = (r[k + 4] - y1) / length(k + 3);
        const double sx = x1 - ux * before;
        const double sy = y1 - uy * before;
        add(ei, part, x0, y0, sx, sy);
        const double c1x = x1 - ux * before * (1 - kHandle);
        const double c1y = y1 - uy * before * (1 - kHandle);
        const double c2x = x1 + vx * after * (1 - kHandle);
        const double c2y = y1 + vy * after * (1 - kHandle);
        at_x = x1 + vx * after;
        at_y = y1 + vy * after;
        const int turn = part | (k + 3 == rn - 3 ? 2 : 0);
        double px = sx;
        double py = sy;
        for (int i = 1; i <= kCornerPieces; i++) {
          const double t = static_cast<double>(i) / kCornerPieces;
          const double u = 1 - t;
          const double x = u * u * u * sx + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * at_x;
          const double y = u * u * u * sy + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * at_y;
          add(ei, turn, px, py, x, y);
          px = x;
          py = y;
        }
        continue;
      }
      if (style == route_style::stepped) {
        const bool down = r[k + 2] == 0;
        const double mx = down ? x0 : (x0 + x1) / 2;
        const double my = down ? (y0 + y1) / 2 : y0;
        add(ei, part, x0, y0, mx, my);
        add(ei, part, mx, my, down ? x1 : mx, down ? my : y1);
        add(ei, part, down ? x1 : mx, down ? my : y1, x1, y1);
        continue;
      }
      // Approximate the rendered S-curve with straight segments.
      const bool down = r[k + 2] == 0;
      const double c = down ? layered_layouter::ease(x1 - x0, y1 - y0) : layered_layouter::ease(y1 - y0, x1 - x0);
      double px = x0;
      double py = y0;
      for (int i = 1; i <= kCurvePieces; i++) {
        const double t = static_cast<double>(i) / kCurvePieces;
        const double u = 1 - t;
        const double across = t * t * (3 - 2 * t);
        const double along = 3 * c * t * u * u + 3 * (1 - c) * t * t * u + t * t * t;
        const double x = x0 + (x1 - x0) * (down ? across : along);
        const double y = y0 + (y1 - y0) * (down ? along : across);
        add(ei, part, px, py, x, y);
        px = x;
        py = y;
      }
    }
  }
  // Count each pair only in the first band shared by both segments.
  const int bands = bottom > top ? static_cast<int>(js_round(std::sqrt(static_cast<double>(count)))) : 1;
  const double scale = bottom > top ? bands / (bottom - top) : 0;
  auto band_of = [&](double y) { return std::min(bands - 1, static_cast<int>(std::floor((y - top) * scale))); };
  std::vector<int> first(count, 0);
  std::vector<int> band_start(bands + 1, 0);
  for (std::size_t p = 0; p < count; p++) {
    first[p] = band_of(low[p]);
    for (int b = first[p], last = band_of(high[p]); b <= last; b++) band_start[b + 1]++;
  }
  for (int b = 0; b < bands; b++) band_start[b + 1] += band_start[b];
  std::vector<int> fill(band_start.begin(), band_start.begin() + bands);
  std::vector<int> in_band(static_cast<std::size_t>(band_start[bands]), 0);
  for (std::size_t p = 0; p < count; p++)
    for (int b = first[p], last = band_of(high[p]); b <= last; b++) in_band[fill[b]++] = static_cast<int>(p);
  // Edges merging into a shared endpoint run do not count as crossings.
  std::vector<double> runs(edges.size() * 8, 0.0);
  auto in_line = [&](const std::vector<double>& r, std::size_t a, std::size_t b, std::size_t c) {
    return (std::abs(r[a] - r[b]) < kInLine && std::abs(r[a] - r[c]) < kInLine) ||
           (std::abs(r[a + 1] - r[b + 1]) < kInLine && std::abs(r[a + 1] - r[c + 1]) < kInLine);
  };
  for (std::size_t ei = 0; ei < edges.size(); ei++) {
    const std::vector<double>& r = edges[ei].route;
    if (r.size() < 6) continue;
    std::size_t k = 3;
    while (k + 3 < r.size() && in_line(r, 0, k, k + 3)) k += 3;
    const std::size_t last = r.size() - 3;
    long long j = static_cast<long long>(last) - 3;
    while (j >= 3 && in_line(r, last, static_cast<std::size_t>(j), static_cast<std::size_t>(j - 3))) j -= 3;
    const double values[8] = {r[0], r[1], r[k], r[k + 1], r[static_cast<std::size_t>(j)], r[static_cast<std::size_t>(j) + 1], r[last], r[last + 1]};
    for (int i = 0; i < 8; i++) runs[ei * 8 + i] = values[i];
  }
  auto on_run = [&](int ei, int end, double x, double y) {
    const std::size_t o = static_cast<std::size_t>(ei) * 8 + (end == 1 ? 0 : 4);
    const double x0 = runs[o];
    const double y0 = runs[o + 1];
    const double dx = runs[o + 2] - x0;
    const double dy = runs[o + 3] - y0;
    const double len = dx * dx + dy * dy;
    const double t = len > 0 ? std::max(0.0, std::min(1.0, ((x - x0) * dx + (y - y0) * dy) / len)) : 0;
    return js_hypot(x - x0 - t * dx, y - y0 - t * dy) < kInLine;
  };
  long long total = 0;
  for (int band = 0; band < bands; band++) {
    const int band_end = band_start[band + 1];
    auto begin = in_band.begin() + band_start[band];
    if (static_cast<std::size_t>(band_end - band_start[band]) > kMaxInsert) {
      std::sort(begin, in_band.begin() + band_end, [&](int p, int q) { return ax[p] < ax[q] || (ax[p] == ax[q] && p < q); });
    } else {
      for (int i = band_start[band] + 1; i < band_end; i++) {
        const int p = in_band[i];
        int j = i;
        for (; j > band_start[band] && ax[in_band[j - 1]] > ax[p]; j--) in_band[j] = in_band[j - 1];
        in_band[j] = p;
      }
    }
    for (int s = band_start[band]; s < band_end; s++) {
      const int p = in_band[s];
      const double dx = bx[p] - ax[p];
      const double dy = by[p] - ay[p];
      const bool here = first[p] == band;
      for (int t = s + 1; t < band_end; t++) {
        const int q = in_band[t];
        if (ax[q] > bx[p]) break;
        if (high[q] < low[p] || low[q] > high[p] || owner[p] == owner[q] || (!here && first[q] != band)) continue;
        const double d1 = dx * (ay[q] - ay[p]) - dy * (ax[q] - ax[p]);
        const double d2 = dx * (by[q] - ay[p]) - dy * (bx[q] - ax[p]);
        // Treat points on the line as one side to count segment-boundary crossings once.
        if ((d1 > 0) == (d2 > 0)) continue;
        const double ex = bx[q] - ax[q];
        const double ey = by[q] - ay[q];
        const double d3 = ex * (ay[p] - ay[q]) - ey * (ax[p] - ax[q]);
        const double d4 = ex * (by[p] - ay[q]) - ey * (bx[p] - ax[q]);
        if ((d3 > 0) == (d4 > 0)) continue;
        const compound_edge& ep = edges[owner[p]];
        const compound_edge& eq = edges[owner[q]];
        const double at = d3 / (d3 - d4);
        const double x = ax[p] + at * dx;
        const double y = ay[p] + at * dy;
        // Exclude fan-in and fan-out at shared nodes.
        auto fan = [&](int v, int pe, int qe) {
          return ((ends[p] & pe) != 0 && (ends[q] & qe) != 0) ||
                 (std::abs(x - nodes[v].x) < nodes[v].w / 2 + kFan && std::abs(y - nodes[v].y) < nodes[v].h / 2 + kFan) ||
                 (on_run(owner[p], pe, x, y) && on_run(owner[q], qe, x, y));
        };
        if (ep.src == eq.src && fan(ep.src, 1, 1)) continue;
        if (ep.src == eq.dst && fan(ep.src, 1, 2)) continue;
        if (ep.dst == eq.src && fan(ep.dst, 2, 1)) continue;
        if (ep.dst == eq.dst && fan(ep.dst, 2, 2)) continue;
        total++;
      }
    }
  }
  return total;
}

enum class side : int { top = 0, right = 1, bottom = 2, left = 3 };

side start_side(flow_dir dir) {
  switch (dir) {
    case flow_dir::tb: return side::top;
    case flow_dir::bt: return side::bottom;
    case flow_dir::lr: return side::left;
    default: return side::right;
  }
}

side opposite(side s) { return static_cast<side>((static_cast<int>(s) + 2) & 3); }

bool is_vertical(flow_dir dir) { return dir == flow_dir::tb || dir == flow_dir::bt; }

// Where an edge crosses the border of a group.
struct port_edge {
  int e = 0;
  int inner = 0;
  bool out = false;
  side where = side::top;
  // Cross-flow position outside the group from a previous layout; NaN if unknown.
  double hint = std::numeric_limits<double>::quiet_NaN();
  double px = 0;
  double py = 0;
  std::vector<double> route;
};

// An edge between two items of one level.
struct level_edge {
  int e = 0;
  int a = 0;
  int b = 0;
};

// The items of one group (or of the diagram) that are laid out together.
class level {
 public:
  level(int id, flow_dir dir) : id_(id), dir_(dir) {}

  int id() const { return id_; }
  flow_dir dir() const { return dir_; }
  bool vertical() const { return is_vertical(dir_); }

  std::vector<int> items;
  std::vector<level_edge> edges;
  std::vector<port_edge> ports;
  std::unordered_map<int, std::vector<double>> routes;
  double x = 0;
  double y = 0;

  port_edge& add_port(int edge, int inner, bool out) {
    ports.push_back(port_edge{edge, inner, out, side::top, std::numeric_limits<double>::quiet_NaN(), 0, 0, {}});
    port_of_[key(edge, out)] = ports.size() - 1;
    return ports.back();
  }
  port_edge& port_for(int edge, bool out) { return ports[port_of_.at(key(edge, out))]; }

 private:
  static int key(int edge, bool out) { return edge * 2 + (out ? 0 : 1); }

  int id_;
  flow_dir dir_;
  std::unordered_map<int, std::size_t> port_of_;
};

// What one run of the layout needs to keep between its steps.
class session {
 public:
  session(const flat_layout_engine& engine, std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, flow_dir root_dir)
      : engine_(engine), nodes_(nodes), edges_(edges), root_dir_(root_dir), n_(static_cast<int>(nodes.size())) {}

  compound_result run() {
    build_hierarchy();
    order_items();
    limit_ports();
    assign_edges_to_levels();
    ordered_ = ports_ > 1 && ports_ <= kMaxOrderedPorts && edges_.size() <= kMaxOrderedEdges;

    // Use a flat layout to coordinate node and port order across groups.
    key_.assign(n_, std::nan(""));
    keyed_ = false;
    flat_ = levels_.size() > 1 && ordered_;
    for (const auto& lv : levels_)
      if (lv->dir() != root_dir_) flat_ = false;
    if (flat_) build_flat_keys();

    // The flat layout ignores group boxes, so independent ordering may yield fewer crossings.
    const route_style style = style_of(engine_.options());
    keyed_ = flat_;
    compound_result total = place();
    long long crossings = -1;
    if (flat_) {
      crossings = route_crossings(nodes_, edges_, style);
      if (static_cast<double>(crossings) > static_cast<double>(flat_crossings_) * kFlatSlack + kFlatSpare) {
        const snapshot first = take_snapshot(total);
        keyed_ = false;
        total = place();
        const long long own = route_crossings(nodes_, edges_, style);
        if (own >= crossings) {
          keyed_ = true;
          total = restore(first);
        } else {
          crossings = own;
        }
      }
    }

    // Refine port order using routes outside each group. Skip this if independent ordering won;
    // it rarely helps enough to justify another layout.
    if (keyed_ == flat_ && ordered_) {
      bool again = false;
      for (auto& lvp : levels_) {
        level& lv = *lvp;
        if (lv.id() < 0) continue;
        const compound_node& box = nodes_[lv.id()];
        const bool vertical = lv.vertical();
        const side end = opposite(start_side(lv.dir()));
        std::vector<std::array<double, 2>> seen;
        for (port_edge& port : lv.ports) {
          if (port.where != end) continue;
          // Skip the straight run out of the port to find where the edge turns.
          const std::vector<double>& route = edges_[port.e].route;
          const double bx = lv.x + port.px;
          const double by = lv.y + port.py;
          long long at = -1;
          const long long rn = static_cast<long long>(route.size());
          for (long long k = 0; k < rn && (at < 0 || port.out); k += 3)
            if (std::abs(route[k] - bx) < 0.01 && std::abs(route[k + 1] - by) < 0.01) at = k;
          if (at < 0) continue;
          const int a = vertical ? 0 : 1;
          long long next = port.out ? at + 3 : at - 3;
          while (next >= 0 && next < rn && std::abs(route[next + a] - route[at + a]) < kInLine) next += port.out ? 3 : -3;
          if (next < 0 || next >= rn) continue;
          port.hint = route[next + a] - (vertical ? box.x - box.w / 2 : box.y - box.h / 2);
          seen.push_back({vertical ? port.px : port.py, port.hint});
        }
        std::stable_sort(seen.begin(), seen.end(), [](const auto& p, const auto& q) { return p[0] < q[0]; });
        for (std::size_t i = 1; i < seen.size(); i++)
          if (seen[i - 1][1] > seen[i][1]) again = true;
      }
      if (again) {
        const long long before = crossings >= 0 ? crossings : route_crossings(nodes_, edges_, style);
        if (before > 0) {
          const snapshot first = take_snapshot(total);
          total = place();
          if (route_crossings(nodes_, edges_, style) >= before) total = restore(first);
        }
      }
    }

    place_stray_and_loops();
    return total;
  }

  // Lays the whole graph out once and gives the result.
  compound_result place() {
    decide_port_sides();
    const compound_result total = lay_out_levels();
    place_levels();
    route_edges();
    return total;
  }

  struct snapshot {
    std::vector<double> nodes;
    std::vector<std::vector<double>> routes;
    std::vector<double> labels;
    std::vector<double> levels;
    compound_result total;
  };

  snapshot take_snapshot(const compound_result& total) const {
    snapshot s;
    for (const compound_node& c : nodes_) {
      s.nodes.push_back(c.x);
      s.nodes.push_back(c.y);
      s.nodes.push_back(c.w);
      s.nodes.push_back(c.h);
    }
    for (const compound_edge& e : edges_) {
      s.routes.push_back(e.route);
      s.labels.push_back(e.label_x);
      s.labels.push_back(e.label_y);
    }
    for (const auto& lv : levels_) {
      s.levels.push_back(lv->x);
      s.levels.push_back(lv->y);
      for (const port_edge& port : lv->ports) {
        s.levels.push_back(port.px);
        s.levels.push_back(port.py);
      }
    }
    s.total = total;
    return s;
  }

  compound_result restore(const snapshot& s) {
    for (std::size_t i = 0; i < nodes_.size(); i++) {
      nodes_[i].x = s.nodes[4 * i];
      nodes_[i].y = s.nodes[4 * i + 1];
      nodes_[i].w = s.nodes[4 * i + 2];
      nodes_[i].h = s.nodes[4 * i + 3];
    }
    for (std::size_t i = 0; i < edges_.size(); i++) {
      edges_[i].route = s.routes[i];
      edges_[i].label_x = s.labels[2 * i];
      edges_[i].label_y = s.labels[2 * i + 1];
    }
    std::size_t k = 0;
    for (auto& lv : levels_) {
      lv->x = s.levels[k++];
      lv->y = s.levels[k++];
      for (port_edge& port : lv->ports) {
        port.px = s.levels[k++];
        port.py = s.levels[k++];
      }
    }
    return s.total;
  }

  // A layout of the graph without group boxes gives each node and port a position to order by.
  void build_flat_keys() {
    const bool vertical = is_vertical(root_dir_);
    std::vector<int> leaf(n_, -1);
    std::vector<int> group(n_, -1);
    std::vector<layered_node> lnodes;
    layered_options::group_map groups;
    for (int i = 0; i < n_; i++) {
      if (!nodes_[i].is_group) continue;
      group[i] = static_cast<int>(groups.parent.size());
      groups.parent.push_back(-1);
    }
    for (int i = 0; i < n_; i++) {
      const int p = nodes_[i].parent;
      if (nodes_[i].is_group) {
        groups.parent[group[i]] = p >= 0 ? group[p] : -1;
      } else {
        leaf[i] = static_cast<int>(lnodes.size());
        lnodes.push_back(layered_node(vertical ? nodes_[i].w : nodes_[i].h, vertical ? nodes_[i].h : nodes_[i].w));
        groups.of.push_back(p >= 0 ? group[p] : -1);
      }
    }
    flat_ledges_.clear();
    flat_ledge_of_.assign(edges_.size(), -1);
    for (std::size_t ei = 0; ei < edges_.size(); ei++) {
      const compound_edge& e = edges_[ei];
      if (e.src == e.dst || leaf[e.src] < 0 || leaf[e.dst] < 0) continue;
      flat_ledge_of_[ei] = static_cast<int>(flat_ledges_.size());
      flat_ledges_.push_back(layered_edge(leaf[e.src], leaf[e.dst], e.minlen, vertical ? e.label_w : e.label_h, vertical ? e.label_h : e.label_w));
    }
    layered_options flat_options = engine_.options();
    flat_options.groups = std::move(groups);
    flat_options.order_only = true;
    const layered_result flat_layout = layered_layouter(flat_options).layout(lnodes, flat_ledges_);
    flat_crossings_ = std::min(flat_layout.crossings, flat_layout.declared);
    std::vector<double> sum(n_, 0.0);
    std::vector<int> count(n_, 0);
    // Last rank occupied by each group.
    flat_last_.assign(n_, -1);
    for (int i = 0; i < n_; i++) {
      if (leaf[i] < 0) continue;
      key_[i] = lnodes[leaf[i]].x;
      for (int p = nodes_[i].parent; p >= 0; p = nodes_[p].parent) {
        sum[p] += key_[i];
        count[p]++;
        flat_last_[p] = std::max(flat_last_[p], lnodes[leaf[i]].rank);
      }
    }
    for (int i = 0; i < n_; i++)
      if (count[i] > 0) key_[i] = sum[i] / count[i];
  }

  // Order outgoing ports at the group's last rank; break ties using the next rank.
  double far_hint(const level& lv, const port_edge& port) const {
    const int le = flat_ledge_of_[port.e];
    if (le < 0 || lv.id() < 0 || !port.out || port.where != opposite(start_side(lv.dir()))) return std::nan("");
    const std::vector<double>& points = flat_ledges_[le].points;
    double inside = key_[edges_[port.e].src];
    double outside = 0;
    for (std::size_t k = 0; k < points.size(); k += 2) {
      if (points[k] == flat_last_[lv.id()]) inside = points[k + 1];
      if (points[k] == flat_last_[lv.id()] + 1) outside = points[k + 1];
    }
    return inside + outside * 1e-6;
  }

 private:
  struct built {
    std::vector<layered_node> lnodes;
    std::vector<layered_edge> ledges;
    std::vector<int> dummies;
  };

  level& make_level(int id, flow_dir dir) {
    levels_.push_back(std::make_unique<level>(id, dir));
    level* l = levels_.back().get();
    level_of_[id] = l;
    return *l;
  }
  level& level_by_id(int id) { return *level_of_.at(id); }
  level& root() { return *levels_.front(); }

  bool endpoint_below(int item, const compound_edge& e, bool out) const { return (out ? e.src : e.dst) != item; }

  void descend(std::size_t from) {
    for (std::size_t q = from; q < levels_.size(); q++) {
      level* lv = levels_[q].get();
      for (int item : lv->items) {
        placed_[item] = 1;
        depth_[item] = lv->id() < 0 ? 0 : depth_[lv->id()] + 1;
        if (nodes_[item].is_group) {
          level& child = make_level(item, nodes_[item].has_dir ? nodes_[item].dir : lv->dir());
          child.items = children_[item];
        }
      }
    }
  }

  void build_hierarchy() {
    make_level(-1, root_dir_);
    children_.assign(n_, {});
    std::vector<int> root_items;
    for (int i = 0; i < n_; i++) {
      const int p = nodes_[i].parent;
      if (p >= 0 && p < n_ && p != i && nodes_[p].is_group) children_[p].push_back(i);
      else {
        nodes_[i].parent = -1;
        root_items.push_back(i);
      }
    }
    root().items = root_items;
    depth_.assign(n_, 0);
    placed_.assign(n_, 0);
    descend(0);
    // Groups that contain each other are unreachable from the root; lift them out.
    for (int i = 0; i < n_; i++) {
      if (placed_[i]) continue;
      const int former = nodes_[i].parent;
      std::vector<int>& siblings = children_[former];
      siblings.erase(std::remove(siblings.begin(), siblings.end(), i), siblings.end());
      nodes_[i].parent = -1;
      root().items.push_back(i);
      placed_[i] = 1;
      depth_[i] = 0;
      if (nodes_[i].is_group) {
        const std::size_t from = levels_.size();
        level& child = make_level(i, nodes_[i].has_dir ? nodes_[i].dir : root_dir_);
        child.items = children_[i];
        descend(from);
      }
    }
  }

  void order_items() {
    for (std::size_t q = levels_.size() - 1; q > 0; q--) {
      const level& lv = *levels_[q];
      double seq = std::numeric_limits<double>::infinity();
      for (int item : lv.items)
        if (nodes_[item].seq < seq) seq = nodes_[item].seq;
      if (seq != std::numeric_limits<double>::infinity()) nodes_[lv.id()].seq = seq;
    }
    for (auto& lv : levels_) {
      std::stable_sort(lv->items.begin(), lv->items.end(), [&](int a, int b) {
        const double d = nodes_[a].seq - nodes_[b].seq;
        return d != 0 ? d < 0 : a < b;
      });
    }
  }

  // Every group border an edge crosses costs a port. Past the budget, an edge is drawn between
  // the groups that hold its ends, at the level they share.
  void limit_ports() {
    int ports = 0;
    for (compound_edge& e : edges_) {
      int a = e.src;
      int b = e.dst;
      int crossed = 0;
      for (; depth_[a] > depth_[b]; crossed++) a = nodes_[a].parent;
      for (; depth_[b] > depth_[a]; crossed++) b = nodes_[b].parent;
      for (; a != b && nodes_[a].parent != nodes_[b].parent; crossed += 2) {
        a = nodes_[a].parent;
        b = nodes_[b].parent;
      }
      if (a == b) continue;
      if (ports + crossed > kMaxPorts) {
        e.src = a;
        e.dst = b;
      } else {
        ports += crossed;
      }
    }
    ports_ = ports;
  }

  void assign_edges_to_levels() {
    for (int ei = 0; ei < static_cast<int>(edges_.size()); ei++) {
      compound_edge& e = edges_[ei];
      e.route.clear();
      if (e.src == e.dst) {
        loops_.push_back(ei);
        continue;
      }
      int a = e.src;
      int b = e.dst;
      std::vector<int> a_path;
      std::vector<int> b_path;
      while (depth_[a] > depth_[b]) {
        a_path.push_back(a);
        a = nodes_[a].parent;
      }
      while (depth_[b] > depth_[a]) {
        b_path.push_back(b);
        b = nodes_[b].parent;
      }
      if (a == b) {
        stray_.push_back(ei);
        continue;
      }
      while (nodes_[a].parent != nodes_[b].parent) {
        a_path.push_back(a);
        b_path.push_back(b);
        a = nodes_[a].parent;
        b = nodes_[b].parent;
      }
      level_by_id(nodes_[a].parent).edges.push_back({ei, a, b});
      for (int inner : a_path) level_by_id(nodes_[inner].parent).add_port(ei, inner, true);
      for (int inner : b_path) level_by_id(nodes_[inner].parent).add_port(ei, inner, false);
    }
    local_.assign(n_, 0);
  }

  // The flat graph of one level. Unsized, it is only good for finding ranks.
  built build(const level& lv, bool sized) {
    const bool vertical = lv.vertical();
    const side start = start_side(lv.dir());
    built out;
    for (std::size_t i = 0; i < lv.items.size(); i++) {
      const int item = lv.items[i];
      local_[item] = static_cast<int>(i);
      const compound_node& node = nodes_[item];
      layered_node ln = sized ? layered_node(vertical ? node.w : node.h, vertical ? node.h : node.w) : layered_node(0, 0);
      ln.span = node.is_group ? std::min(node.span, ln.w - 2 * node.pad_x) : node.span;
      ln.key = keyed_ ? key_[item] : std::nan("");
      out.lnodes.push_back(ln);
    }
    auto held = [&](int item, int ei, bool o) { return nodes_[item].is_group && endpoint_below(item, edges_[ei], o); };
    auto offset = [&](int item, int ei, bool o) -> double {
      if (!sized || !nodes_[item].is_group || !endpoint_below(item, edges_[ei], o)) return 0;
      const port_edge& port = level_by_id(item).port_for(ei, o);
      return vertical ? port.px - nodes_[item].w / 2 : port.py - nodes_[item].h / 2;
    };
    for (const level_edge& le : lv.edges) {
      const compound_edge& e = edges_[le.e];
      layered_edge edge(local_[le.a], local_[le.b], e.minlen);
      if (sized) {
        edge.label_w = vertical ? e.label_w : e.label_h;
        edge.label_h = vertical ? e.label_h : e.label_w;
        edge.tail_dx = offset(le.a, le.e, true);
        edge.head_dx = offset(le.b, le.e, false);
        edge.tail_fixed = held(le.a, le.e, true);
        edge.head_fixed = held(le.b, le.e, false);
      }
      out.ledges.push_back(edge);
    }
    out.dummies.assign(lv.ports.size(), -1);
    for (std::size_t i = 0; i < lv.ports.size(); i++) {
      const port_edge& port = lv.ports[i];
      if (port.where == start || port.where == opposite(start)) {
        const int d = static_cast<int>(out.lnodes.size());
        layered_node ln(0, 0, port.where == start ? node_kind::start_port : node_kind::end_port);
        ln.key = keyed_ ? key_[port.out ? edges_[port.e].dst : edges_[port.e].src] : std::nan("");
        ln.hint = std::isnan(port.hint) && keyed_ && flat_ ? far_hint(lv, port) : port.hint;
        out.lnodes.push_back(ln);
        out.dummies[i] = d;
        layered_edge edge = port.out ? layered_edge(local_[port.inner], d) : layered_edge(d, local_[port.inner]);
        if (port.out) {
          edge.tail_dx = offset(port.inner, port.e, true);
          edge.tail_fixed = held(port.inner, port.e, true);
        } else {
          edge.head_dx = offset(port.inner, port.e, false);
          edge.head_fixed = held(port.inner, port.e, false);
        }
        out.ledges.push_back(edge);
      } else {
        out.lnodes[local_[port.inner]].pin = port.where == side::top || port.where == side::left ? -1 : 1;
      }
    }
    return out;
  }

  // Top-down: rank each level to learn which side of a group every crossing edge leaves from.
  void decide_port_sides() {
    for (std::size_t q = 0; q < levels_.size(); q++) {
      level& lv = *levels_[q];
      for (const port_edge& port : lv.ports) {
        if (nodes_[port.inner].is_group && endpoint_below(port.inner, edges_[port.e], port.out))
          level_by_id(port.inner).port_for(port.e, port.out).where = port.where;
      }
      if (lv.edges.empty()) continue;
      bool needed = false;
      for (const level_edge& le : lv.edges) {
        const compound_edge& e = edges_[le.e];
        if ((nodes_[le.a].is_group && e.src != le.a) || (nodes_[le.b].is_group && e.dst != le.b)) needed = true;
      }
      if (!needed) continue;
      built b = build(lv, false);
      engine_.rank_nodes(b.lnodes, b.ledges);
      const side start = start_side(lv.dir());
      for (std::size_t i = 0; i < lv.edges.size(); i++) {
        const level_edge& le = lv.edges[i];
        const compound_edge& e = edges_[le.e];
        const bool a_above = !b.ledges[i].reversed;
        if (nodes_[le.a].is_group && e.src != le.a) level_by_id(le.a).port_for(le.e, true).where = a_above ? opposite(start) : start;
        if (nodes_[le.b].is_group && e.dst != le.b) level_by_id(le.b).port_for(le.e, false).where = a_above ? start : opposite(start);
      }
    }
  }

  // Reuse unchanged levels across layout attempts.
  struct laid {
    std::vector<double> given;
    std::vector<layered_node> lnodes;
    std::vector<layered_edge> ledges;
    layered_result size;
  };

  static bool same_numbers(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
      // Object.is: NaN equals NaN, and 0 differs from -0.
      if (std::isnan(a[i]) && std::isnan(b[i])) continue;
      if (std::memcmp(&a[i], &b[i], sizeof(double)) != 0) return false;
    }
    return true;
  }

  std::shared_ptr<laid> lay_out(const level& lv, built& made) {
    if (!ordered_) {
      auto fresh = std::make_shared<laid>();
      fresh->lnodes = std::move(made.lnodes);
      fresh->ledges = std::move(made.ledges);
      fresh->size = engine_.layout(fresh->lnodes, fresh->ledges);
      return fresh;
    }
    std::vector<double> given;
    for (const layered_node& ln : made.lnodes) {
      given.insert(given.end(), {ln.w, ln.h, static_cast<double>(ln.kind), static_cast<double>(ln.pin), ln.hint, ln.key, ln.span});
    }
    for (const layered_edge& le : made.ledges) {
      given.insert(given.end(), {static_cast<double>(le.tail), static_cast<double>(le.head), static_cast<double>(le.minlen), le.label_w, le.label_h, le.tail_dx,
                                 le.head_dx, le.tail_fixed ? 1.0 : 0.0, le.head_fixed ? 1.0 : 0.0});
    }
    std::vector<std::shared_ptr<laid>>& past = laid_[lv.id()];
    for (const std::shared_ptr<laid>& p : past)
      if (same_numbers(p->given, given)) return p;
    auto done = std::make_shared<laid>();
    done->given = std::move(given);
    done->lnodes = std::move(made.lnodes);
    done->ledges = std::move(made.ledges);
    done->size = engine_.layout(done->lnodes, done->ledges);
    past.push_back(done);
    return done;
  }

  // Bottom-up: lay out each level now that the sizes of its groups are known.
  compound_result lay_out_levels() {
    compound_result total;
    for (std::size_t q = levels_.size(); q-- > 0;) {
      level& lv = *levels_[q];
      compound_node* group = lv.id() >= 0 ? &nodes_[lv.id()] : nullptr;
      const bool vertical = lv.vertical();
      const bool flip = lv.dir() == flow_dir::bt || lv.dir() == flow_dir::rl;
      built made = build(lv, true);
      const std::shared_ptr<laid> done = lay_out(lv, made);
      built b;
      b.lnodes = done->lnodes;
      b.ledges = done->ledges;
      b.dummies = made.dummies;
      const layered_result size = done->size;
      const double content_w = vertical ? size.width : size.height;
      const double content_h = vertical ? size.height : size.width;
      const double pad_x = group ? group->pad_x : 0;
      const double pad_top = group ? group->pad_top : 0;
      const double box_w = std::max(content_w + 2 * pad_x, group ? group->min_w : 0);
      const double box_h = content_h + pad_top + (group ? group->pad_bottom : 0);
      const double off_x = (box_w - content_w) / 2;
      const double off_y = pad_top;
      auto tx = [&](double lx, double ly) { return off_x + (vertical ? lx : flip ? size.height - ly : ly); };
      auto ty = [&](double lx, double ly) { return off_y + (vertical ? (flip ? size.height - ly : ly) : lx); };
      const double axis = vertical ? 0 : 1;
      auto convert = [&](const std::vector<double>& points) {
        std::vector<double> out;
        for (std::size_t i = 0; i < points.size(); i += 2) {
          out.push_back(tx(points[i], points[i + 1]));
          out.push_back(ty(points[i], points[i + 1]));
          out.push_back(axis);
        }
        return out;
      };

      for (std::size_t i = 0; i < lv.items.size(); i++) {
        compound_node& node = nodes_[lv.items[i]];
        node.x = tx(b.lnodes[i].x, b.lnodes[i].y);
        node.y = ty(b.lnodes[i].x, b.lnodes[i].y);
      }
      for (std::size_t i = 0; i < lv.edges.size(); i++) {
        const layered_edge& edge = b.ledges[i];
        lv.routes[lv.edges[i].e] = convert(edge.points);
        compound_edge& e = edges_[lv.edges[i].e];
        if (e.label_w > 0) {
          e.label_x = tx(edge.label_x, edge.label_y);
          e.label_y = ty(edge.label_x, edge.label_y);
        }
      }
      std::size_t k = lv.edges.size();
      for (std::size_t i = 0; i < lv.ports.size(); i++) {
        port_edge& port = lv.ports[i];
        const compound_node& item = nodes_[port.inner];
        if (b.dummies[i] >= 0) {
          std::vector<double> route = convert(b.ledges[k++].points);
          const std::size_t at = port.out ? route.size() - 3 : 0;
          double bx = route[at];
          double by = route[at + 1];
          if (port.where == side::top) by = 0;
          else if (port.where == side::bottom) by = box_h;
          else if (port.where == side::left) bx = 0;
          else bx = box_w;
          if (port.out) {
            route.push_back(bx);
            route.push_back(by);
            route.push_back(axis);
          } else {
            route.insert(route.begin(), {bx, by, axis});
          }
          port.px = bx;
          port.py = by;
          port.route = std::move(route);
          continue;
        }
        double ix = item.x;
        double iy = item.y;
        if (item.is_group && endpoint_below(port.inner, edges_[port.e], port.out)) {
          const port_edge& inner = level_by_id(port.inner).port_for(port.e, port.out);
          ix += inner.px - item.w / 2;
          iy += inner.py - item.h / 2;
        } else if (port.where == side::top) iy -= item.h / 2;
        else if (port.where == side::bottom) iy += item.h / 2;
        else if (port.where == side::left) ix -= item.w / 2;
        else ix += item.w / 2;
        double bx = ix;
        double by = iy;
        if (port.where == side::top) by = 0;
        else if (port.where == side::bottom) by = box_h;
        else if (port.where == side::left) bx = 0;
        else bx = box_w;
        const double a = port.where == side::top || port.where == side::bottom ? 0 : 1;
        port.px = bx;
        port.py = by;
        port.route = port.out ? std::vector<double>{ix, iy, a, bx, by, a} : std::vector<double>{bx, by, a, ix, iy, a};
      }

      if (group) {
        group->w = box_w;
        group->h = box_h;
      } else {
        total = {box_w, box_h};
      }
    }
    return total;
  }

  // Top-down again: turn positions inside each box into diagram coordinates.
  void place_levels() {
    for (auto& lv : levels_) {
      const double ox = lv->x;
      const double oy = lv->y;
      for (int item : lv->items) {
        compound_node& node = nodes_[item];
        node.x += ox;
        node.y += oy;
        if (node.is_group) {
          level& child = level_by_id(item);
          child.x = node.x - node.w / 2;
          child.y = node.y - node.h / 2;
        }
      }
    }
  }

  static void append(std::vector<double>& route, const std::vector<double>& part, double ox, double oy) {
    for (std::size_t i = 0; i < part.size(); i += 3) {
      const double x = part[i] + ox;
      const double y = part[i + 1] + oy;
      const std::size_t len = route.size();
      if (len >= 3 && std::abs(route[len - 3] - x) < 0.01 && std::abs(route[len - 2] - y) < 0.01) continue;
      route.push_back(x);
      route.push_back(y);
      route.push_back(part[i + 2]);
    }
  }

  // Joins, for each edge, the pieces of its route that the levels it passes through hold.
  void route_edges() {
    for (auto& lvp : levels_) {
      level& lv = *lvp;
      for (const level_edge& le : lv.edges) {
        compound_edge& e = edges_[le.e];
        std::vector<double> route;
        std::vector<level*> outward;
        for (int item = e.src; nodes_[item].parent != lv.id(); item = nodes_[item].parent) outward.push_back(&level_by_id(nodes_[item].parent));
        for (level* l : outward) append(route, l->port_for(le.e, true).route, l->x, l->y);
        append(route, lv.routes.at(le.e), lv.x, lv.y);
        std::vector<level*> inward;
        for (int item = e.dst; nodes_[item].parent != lv.id(); item = nodes_[item].parent) inward.push_back(&level_by_id(nodes_[item].parent));
        for (std::size_t i = inward.size(); i-- > 0;) append(route, inward[i]->port_for(le.e, false).route, inward[i]->x, inward[i]->y);
        e.route = std::move(route);
        if (e.label_w > 0) {
          e.label_x += lv.x;
          e.label_y += lv.y;
        }
      }
    }
  }

  void place_stray_and_loops() {
    for (int ei : stray_) {
      compound_edge& e = edges_[ei];
      const compound_node& a = nodes_[e.src];
      const compound_node& b = nodes_[e.dst];
      e.route = {a.x, a.y, 0, b.x, b.y, 0};
      e.label_x = (a.x + b.x) / 2;
      e.label_y = (a.y + b.y) / 2;
    }
    for (int ei : loops_) {
      compound_edge& e = edges_[ei];
      const compound_node& node = nodes_[e.src];
      e.label_x = node.x + node.w / 2 + 28 + e.label_w / 2;
      e.label_y = node.y;
    }
  }

  const flat_layout_engine& engine_;
  std::vector<compound_node>& nodes_;
  std::vector<compound_edge>& edges_;
  flow_dir root_dir_;
  int n_;
  std::vector<std::unique_ptr<level>> levels_;
  std::unordered_map<int, level*> level_of_;
  std::vector<std::vector<int>> children_;
  std::vector<int> depth_;
  std::vector<std::uint8_t> placed_;
  std::vector<int> local_;
  std::vector<int> loops_;
  std::vector<int> stray_;
  int ports_ = 0;
  bool ordered_ = false;
  bool flat_ = false;
  bool keyed_ = false;
  long long flat_crossings_ = 0;
  std::vector<double> key_;
  std::vector<layered_edge> flat_ledges_;
  std::vector<int> flat_ledge_of_;
  std::vector<int> flat_last_;
  std::unordered_map<int, std::vector<std::shared_ptr<laid>>> laid_;
};

}  // namespace

compound_result compound_layouter::layout(std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, flow_dir root_dir) const {
  return session(engine_, nodes, edges, root_dir).run();
}

}  // namespace pele
