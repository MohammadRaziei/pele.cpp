#pragma once

#include <string_view>
#include <vector>

#include "layered.h"

namespace pele {

enum class flow_dir { tb, bt, lr, rl };

// The direction a diagram names, or top to bottom for anything else.
flow_dir parse_direction(std::string_view dir);

struct compound_node {
  compound_node() = default;
  compound_node(double w_, double h_, int parent_ = -1) : w(w_), h(h_), parent(parent_) {}

  double w = 0;
  double h = 0;
  int parent = -1;
  bool is_group = false;
  bool has_dir = false;
  flow_dir dir = flow_dir::tb;
  double pad_x = 0;
  double pad_top = 0;
  double pad_bottom = 0;
  double min_w = 0;
  // Declaration order; a group takes the smallest value among its members.
  double seq = 0;
  // How much of a side its edges may spread along, measured across the flow; 0 keeps them at its middle.
  double span = 0;
  double x = 0;
  double y = 0;
};

struct compound_edge {
  compound_edge() = default;
  compound_edge(int src_, int dst_, int minlen_ = 1, double label_w_ = 0, double label_h_ = 0)
      : src(src_), dst(dst_), minlen(minlen_), label_w(label_w_), label_h(label_h_) {}

  int src = 0;
  int dst = 0;
  int minlen = 1;
  double label_w = 0;
  double label_h = 0;
  // x, y, axis triples; axis is 0 where the edge runs vertically into the point, 1 horizontally.
  std::vector<double> route;
  double label_x = 0;
  double label_y = 0;
};

struct compound_result {
  double width = 0;
  double height = 0;
};

// Lays out a graph whose nodes may be nested in groups. Each group is laid out on its own, in its
// own direction, by the flat engine it is given, and then placed in its parent as a single box.
// An edge that crosses a group's border passes through a port on the side facing its other end.
class compound_layouter {
 public:
  explicit compound_layouter(const flat_layout_engine& engine) : engine_(engine) {}

  compound_result layout(std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, flow_dir root_dir) const;

  // Moves a finished layout, its edges and labels included.
  static void shift(std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, double ox, double oy);

 private:
  const flat_layout_engine& engine_;
};

}  // namespace pele
