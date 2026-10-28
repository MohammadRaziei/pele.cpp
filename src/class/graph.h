#pragma once

#include <map>
#include <optional>
#include <vector>

#include "class/types.h"
#include "ustring.h"

namespace pele::classdiagram {

enum class node_shape { rect, class_box, note, interface };
enum class end_marker { none, aggregation, extension, composition, dependency, lollipop };
enum class edge_pattern { solid, dashed, dotted };

struct graph_node {
  ustring id;
  ustring label;
  bool is_group = false;
  node_shape shape = node_shape::rect;
  std::optional<ustring> parent_id;
  // Index of the namespace node this one is drawn in, or -1.
  int parent = -1;
  const class_node* cls = nullptr;
  // Position among the classes, in declaration order. Namespaces and notes have none.
  int color_index = -1;
};

struct graph_edge {
  ustring id;
  ustring start;
  ustring end;
  // Indexes into the node list. Ids alone are ambiguous: a class may be named `note0` or `interface0`.
  int source = -1;
  int target = -1;
  ustring label;
  end_marker arrow_start = end_marker::none;
  end_marker arrow_end = end_marker::none;
  ustring start_label_right;
  ustring end_label_left;
  edge_pattern pattern = edge_pattern::solid;
};

struct class_graph {
  std::vector<graph_node> nodes;
  std::vector<graph_edge> edges;
};

// Resolves a parsed class diagram into the nodes and edges that are drawn. With `hierarchical`
// off, only namespaces the source declares are kept and each is labeled with its full name.
class_graph build_class_graph(const class_model& model, bool hierarchical = true);

}  // namespace pele::classdiagram
