#include "components.h"

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>

namespace pele {
namespace {

struct part {
  std::vector<std::size_t> nodes;
  std::vector<std::size_t> edges;
  double left = std::numeric_limits<double>::infinity();
  double top = std::numeric_limits<double>::infinity();
  double right = -std::numeric_limits<double>::infinity();
  double bottom = -std::numeric_limits<double>::infinity();

  void include(double x, double y, double w = 0, double h = 0) {
    left = std::min(left, x - w / 2);
    top = std::min(top, y - h / 2);
    right = std::max(right, x + w / 2);
    bottom = std::max(bottom, y + h / 2);
  }
};

}  // namespace

std::optional<compound_result> wrap_components(std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, double width, double gap) {
  std::vector<int> parent(nodes.size());
  std::iota(parent.begin(), parent.end(), 0);
  auto root = [&](int i) {
    while (parent[i] != i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  auto join = [&](int a, int b) { parent[root(a)] = root(b); };
  for (std::size_t i = 0; i < nodes.size(); i++)
    if (nodes[i].parent >= 0) join(static_cast<int>(i), nodes[i].parent);
  for (const compound_edge& e : edges) join(e.src, e.dst);

  // Parts in the order their first node appears.
  std::vector<part> parts;
  std::map<int, std::size_t> part_of;
  for (std::size_t i = 0; i < nodes.size(); i++) {
    const int id = root(static_cast<int>(i));
    auto it = part_of.find(id);
    if (it == part_of.end()) {
      it = part_of.emplace(id, parts.size()).first;
      parts.emplace_back();
    }
    part& p = parts[it->second];
    const compound_node& c = nodes[i];
    p.nodes.push_back(i);
    p.include(c.x, c.y, c.w, c.h);
  }
  if (parts.size() < 2) return std::nullopt;
  for (std::size_t ei = 0; ei < edges.size(); ei++) {
    const compound_edge& e = edges[ei];
    part& p = parts[part_of[root(e.src)]];
    p.edges.push_back(ei);
    for (std::size_t i = 0; i + 1 < e.route.size(); i += 3) p.include(e.route[i], e.route[i + 1]);
    if (e.label_w > 0) p.include(e.label_x, e.label_y, e.label_w, e.label_h);
  }

  double x = 0;
  double y = 0;
  double row_height = 0;
  double right = 0;
  double from = std::numeric_limits<double>::infinity();
  double to = -std::numeric_limits<double>::infinity();
  std::vector<double> shifts;
  for (const part& p : parts) {
    const double w = p.right - p.left;
    const double h = p.bottom - p.top;
    if (x > 0 && x + w > width) {
      y += row_height + gap;
      x = 0;
      row_height = 0;
    }
    shifts.push_back(x - p.left);
    shifts.push_back(y - p.top);
    from = std::min(from, p.left);
    to = std::max(to, p.right);
    right = std::max(right, x + w);
    row_height = std::max(row_height, h);
    x += w + gap;
  }
  if (right >= to - from) return std::nullopt;
  std::size_t k = 0;
  for (const part& p : parts) {
    std::vector<compound_node> ns;
    std::vector<compound_edge> es;
    for (std::size_t i : p.nodes) ns.push_back(nodes[i]);
    for (std::size_t i : p.edges) es.push_back(edges[i]);
    compound_layouter::shift(ns, es, shifts[k], shifts[k + 1]);
    k += 2;
    for (std::size_t j = 0; j < p.nodes.size(); j++) nodes[p.nodes[j]] = ns[j];
    for (std::size_t j = 0; j < p.edges.size(); j++) edges[p.edges[j]] = es[j];
  }
  compound_result result;
  result.width = right;
  result.height = y + row_height;
  return result;
}

}  // namespace pele
