#include "compound.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>

namespace pele {

flow_dir parse_direction(std::string_view dir) {
  if (dir == "BT") return flow_dir::bt;
  if (dir == "LR") return flow_dir::lr;
  if (dir == "RL") return flow_dir::rl;
  return flow_dir::tb;
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
    ports.push_back(port_edge{edge, inner, out, side::top, 0, 0, {}});
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
    decide_port_sides();
    const compound_result total = lay_out_levels();
    place_levels();
    route_edges();
    place_stray_and_loops();
    return total;
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
        out.lnodes.push_back(layered_node(0, 0, port.where == start ? node_kind::start_port : node_kind::end_port));
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

  // Bottom-up: lay out each level now that the sizes of its groups are known.
  compound_result lay_out_levels() {
    compound_result total;
    for (std::size_t q = levels_.size(); q-- > 0;) {
      level& lv = *levels_[q];
      compound_node* group = lv.id() >= 0 ? &nodes_[lv.id()] : nullptr;
      const bool vertical = lv.vertical();
      const bool flip = lv.dir() == flow_dir::bt || lv.dir() == flow_dir::rl;
      built b = build(lv, true);
      const layered_result size = engine_.layout(b.lnodes, b.ledges);
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
};

}  // namespace

compound_result compound_layouter::layout(std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, flow_dir root_dir) const {
  return session(engine_, nodes, edges, root_dir).run();
}

}  // namespace pele
