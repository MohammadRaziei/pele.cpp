#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace pele {

// Layered (Sugiyama-style) layout of one flat graph. Ranks run down the y axis; callers rotate
// the result for other directions. A C++ rewrite of Pele's layout/layered.ts; the order of every
// floating point operation is kept, so identical input gives identical output.

enum class node_kind : std::uint8_t { node = 0, dummy = 1, label = 2, start_port = 3, end_port = 4 };

struct layered_node {
  layered_node() = default;
  layered_node(double w_, double h_, node_kind kind_ = node_kind::node) : w(w_), h(h_), kind(kind_) {}

  double w = 0;
  double h = 0;
  node_kind kind = node_kind::node;
  int pin = 0;
  // Port's cross-flow destination; NaN if unknown. Fully hinted port ranks keep this order.
  double hint = std::nan("");
  // Initial cross-flow position; NaN if unknown. Used only when every node in a rank has one.
  double key = std::nan("");
  // How much of the side that edges meet may be shared out between them; 0 keeps them all at its middle.
  double span = 0;
  double x = 0;
  double y = 0;
  int rank = 0;
};

struct layered_edge {
  layered_edge() = default;
  layered_edge(int tail_, int head_, int minlen_ = 1, double label_w_ = 0, double label_h_ = 0)
      : tail(tail_), head(head_), minlen(minlen_), label_w(label_w_), label_h(label_h_) {}

  int tail = 0;
  int head = 0;
  int minlen = 1;
  double label_w = 0;
  double label_h = 0;
  double tail_dx = 0;
  double head_dx = 0;
  // An end that is held where its offset puts it, and takes no part in sharing out a node's side.
  bool tail_fixed = false;
  bool head_fixed = false;
  bool reversed = false;
  std::vector<double> points;
  double label_x = 0;
  double label_y = 0;
};

struct layered_options {
  double node_sep = 0;
  double edge_sep = 0;
  double rank_sep = 0;
  double port_sep = 0;
  // Route edges at right angles, each on a track of its own across the gap between two ranks.
  bool tracks = false;
  // Draw every edge straight from point to point: none is given a level to turn onto.
  bool straight = false;
  // The caller sets the ends of the edges on one side of a node apart itself.
  bool apart = false;
  // The room that the marker at the head of an edge takes.
  double head_room = 0;
  // Keep each group contiguous within ranks. `of` maps nodes to groups; `parent` maps groups to
  // parents. Both use -1 for none.
  struct group_map {
    std::vector<int> of;
    std::vector<int> parent;
  };
  std::optional<group_map> groups;
  // Compute cross-flow positions in `x` without full routing.
  bool order_only = false;
};

struct layered_result {
  double width = 0;
  double height = 0;
  // With `order_only`: final crossings and crossings before grouping or reordering.
  long long crossings = 0;
  long long declared = 0;
};

// A layout of one flat graph. Strategy: callers that lay out nested graphs depend on this
// interface and not on one algorithm.
class flat_layout_engine {
 public:
  virtual ~flat_layout_engine() = default;

  // Places the nodes and routes the edges; the result is the size of the drawing.
  virtual layered_result layout(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges) const = 0;

  // Only decides which way each edge runs and which rank each node is in (node.rank, edge.reversed).
  virtual void rank_nodes(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges) const = 0;

  // What the engine was set up with.
  virtual const layered_options& options() const = 0;
};

class layered_layouter final : public flat_layout_engine {
 public:
  explicit layered_layouter(layered_options options) : options_(options) {}

  layered_result layout(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges) const override;
  void rank_nodes(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges) const override;

  const layered_options& options() const override { return options_; }

  // Reverses the edges that close a cycle, found by depth-first search in declaration order.
  static void orient(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges);
  static void assign_ranks(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges, int step);

  // How far along the flow the handles of a plain S reach, as a share of the way it travels along it.
  static double ease(double across, double along);

 private:
  layered_options options_;
};

}  // namespace pele
