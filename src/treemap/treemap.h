#pragma once

#include <optional>
#include <vector>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::treemap {

// A row of the text: a section or a leaf with its indentation and class, or a class definition.
struct row_ast {
  bool class_def = false;
  std::optional<int> indent;
  // For a node.
  bool leaf = false;
  ustring name;
  double value = 0;
  std::optional<ustring> class_selector;
  // For a class definition.
  ustring class_name;
  std::optional<ustring> style_text;
};

struct treemap_ast {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  std::vector<row_ast> rows;
};

treemap_ast parse_treemap(const ustring& source);

struct treemap_node {
  ustring name;
  // A section has a list of children, which may be empty. A leaf has none.
  bool section = false;
  std::optional<double> value;
  std::optional<ustring> class_selector;
  std::optional<std::vector<ustring>> compiled_styles;
  std::vector<int> children;
};

struct treemap_model {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  // Every node, and the top-level ones among them in source order.
  std::vector<treemap_node> nodes;
  std::vector<int> roots;
};

treemap_model build_treemap(const treemap_ast& ast, const std::optional<ustring>& title);

rendered render_treemap(const treemap_model& model, const config& settings, const render_options& options);

}  // namespace pele::treemap
