#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ustring.h"
#include "yaml.h"

namespace pele::flowchart {

enum class label_type { text, string, markdown };

struct flow_text {
  ustring text;
  label_type type = label_type::text;
};

// A node, in the order Mermaid reads it.
struct flow_node {
  ustring id;
  std::optional<ustring> text;
  label_type label = label_type::text;
  std::optional<std::string> type;
  std::vector<ustring> styles;
  std::vector<ustring> classes;
  std::optional<std::string> dir;
  std::vector<std::pair<ustring, ustring>> props;
  std::optional<ustring> link;
  std::optional<ustring> link_target;
  std::optional<ustring> icon;
  std::optional<ustring> form;
  std::optional<ustring> pos;
  std::optional<ustring> img;
  std::optional<ustring> constraint;
  std::optional<double> asset_width;
  std::optional<double> asset_height;
};

struct flow_edge {
  ustring start;
  ustring end;
  std::optional<std::string> type;
  ustring text;
  label_type label = label_type::text;
  std::optional<std::string> stroke;
  std::optional<double> length;
  std::optional<std::vector<ustring>> style;
  std::vector<ustring> classes;
  std::optional<ustring> id;
  bool is_user_defined_id = false;
  std::optional<std::string> interpolate;
  // Taken from the metadata of an edge as written, whatever its type.
  std::optional<yaml_value> animate;
  std::optional<yaml_value> animation;
};

struct flow_class_def {
  ustring id;
  std::vector<ustring> styles;
  std::vector<ustring> text_styles;
};

struct flow_subgraph {
  ustring id;
  std::vector<ustring> nodes;
  ustring title;
  std::vector<ustring> classes;
  std::optional<std::string> dir;
  label_type label = label_type::markdown;
  yaml_value metadata;
  bool has_metadata = false;
};

// What the parser learns about a link: its type and stroke, and its text and id where it has them.
struct link_info {
  std::string type;
  std::string stroke;
  std::optional<double> length;
  std::optional<flow_text> text;
  std::optional<ustring> id;
};

// An item of a subgraph's body: a node name, a list of them, or a direction statement.
struct doc_item {
  enum class kind { name, names, direction } what = kind::name;
  ustring name;
  std::vector<ustring> names;
  std::string direction;
};

}  // namespace pele::flowchart
