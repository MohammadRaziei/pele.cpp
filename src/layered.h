#pragma once

#include <cstdint>
#include <vector>

namespace pele {

// Layered (Sugiyama-style) layout of one flat graph. Ranks run down the y axis; callers rotate
// the result for other directions. A C++ rewrite of Pele's layout/layered.ts; the order of every
// floating point operation is kept, so identical input gives identical output.

enum class node_kind : std::uint8_t { node = 0, dummy = 1, label = 2, start_port = 3, end_port = 4 };

struct layered_node {
  double w = 0;
  double h = 0;
  node_kind kind = node_kind::node;
  int pin = 0;
  // How much of the side that edges meet may be shared out between them; 0 keeps them all at its middle.
  double span = 0;
  double x = 0;
  double y = 0;
  int rank = 0;
};

struct layered_edge {
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
};

struct layered_result {
  double width = 0;
  double height = 0;
};

layered_node make_layered_node(double w, double h, node_kind kind = node_kind::node);
layered_edge make_layered_edge(int tail, int head, int minlen = 1, double label_w = 0, double label_h = 0);

// Reverses the edges that close a cycle, found by depth-first search in declaration order.
void orient(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges);
void assign_ranks(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges, int step);
layered_result layered_layout(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges, const layered_options& opt);

// How far along the flow the handles of a plain S reach, as a share of the way it travels along it.
double ease(double across, double along);

}  // namespace pele
