#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::er {

// Mermaid's cardinalities. The names are the ones the markers are drawn by, lower-cased.
enum class cardinality { zero_or_one, zero_or_more, one_or_more, only_one, md_parent };

struct er_attribute {
  ustring type;
  ustring name;
  std::vector<ustring> keys;
  ustring comment;
};

struct er_entity {
  ustring id;
  ustring label;
  ustring alias;
  std::vector<er_attribute> attributes;
  ustring css_classes = u"default";
  std::vector<ustring> css_styles;
};

// card_b sits at entity A's end of the line and card_a at entity B's, as in Mermaid.
struct er_relationship {
  ustring entity_a;
  ustring role_a;
  ustring entity_b;
  cardinality card_a = cardinality::only_one;
  bool identifying = false;
  cardinality card_b = cardinality::only_one;
};

struct er_class_def {
  ustring id;
  std::vector<ustring> styles;
  std::vector<ustring> text_styles;
};

struct er_subgraph {
  ustring id;
  std::vector<ustring> nodes;
  ustring title;
  std::vector<ustring> classes;
  std::vector<ustring> css_styles;
  std::optional<ustring> dir;
};

// Lists keep the order of insertion, as the maps of Mermaid's model do; the indices find an entry by name.
struct er_model {
  ustring direction = u"TB";
  std::vector<er_entity> entities;
  std::map<ustring, int> entity_index;
  std::vector<er_relationship> relationships;
  std::vector<er_class_def> classes;
  std::map<ustring, int> class_index;
  std::vector<er_subgraph> subgraphs;
  std::map<ustring, int> subgraph_index;
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
};

// Reads the text of an ER diagram into `model`. A syntax error is a pele_error worded as Mermaid words it.
void parse_er(const ustring& source, er_model& model);

rendered render_er(const er_model& model, const config& settings, const render_options& options);

}  // namespace pele::er
