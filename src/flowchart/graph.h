#pragma once

#include <optional>
#include <string>
#include <vector>

#include "db.h"

namespace pele::flowchart {

// A node as it is drawn: a vertex or a subgraph, with its styles settled.
struct graph_node {
  ustring id;
  std::optional<ustring> label;
  label_type label_kind = label_type::text;
  std::string shape;
  bool is_group = false;
  std::optional<ustring> parent_id;
  std::vector<ustring> css_styles;
  std::vector<ustring> css_compiled_styles;
  ustring css_classes;
  std::optional<std::string> dir;
  std::optional<ustring> link;
  std::optional<ustring> link_target;
  std::optional<ustring> tooltip;
  std::optional<ustring> icon;
  std::optional<ustring> pos;
  std::optional<ustring> img;
  std::optional<double> asset_width;
  std::optional<double> asset_height;
  std::optional<ustring> constraint;
  std::optional<int> color_index;
  yaml_value metadata;
  bool has_metadata = false;
};

struct graph_edge {
  ustring id;
  bool is_user_defined_id = false;
  ustring start;
  ustring end;
  std::string type;
  ustring label;
  label_type label_kind = label_type::text;
  std::optional<std::string> thickness;
  std::optional<std::string> pattern;
  std::optional<double> minlen;
  std::string arrow_type_start;
  std::string arrow_type_end;
  std::vector<ustring> css_compiled_styles;
  std::vector<ustring> style;
  ustring classes;
  std::optional<yaml_value> animate;
  std::optional<yaml_value> animation;
  std::optional<std::string> curve;
};

struct flow_graph {
  std::vector<graph_node> nodes;
  std::vector<graph_edge> edges;
};

// Resolves a parsed flowchart into the nodes and edges that are actually drawn: group membership,
// collapsed subgraphs, class styles and edge defaults.
flow_graph build_flow_graph(const flow_db& model, const std::optional<std::string>& default_curve = std::nullopt);

}  // namespace pele::flowchart
