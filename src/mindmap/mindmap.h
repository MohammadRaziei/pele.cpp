#pragma once

#include <optional>
#include <vector>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::mindmap {

struct mindmap_node {
  // The node's place in `nodes`, which is also its place in the source.
  int id = 0;
  ustring node_id;
  int level = 0;
  ustring descr;
  int type = 0;
  std::vector<int> children;
  bool is_root = false;
  // Index of the first-level branch the node belongs to, wrapping after eleven as in Mermaid.
  std::optional<int> section;
  std::optional<ustring> css_class;
  std::optional<ustring> icon;
};

struct mindmap_model {
  std::optional<ustring> title;
  // In source order, which is depth-first. The first node is the root.
  std::vector<mindmap_node> nodes;
};

// Reads the text of a mindmap into a model. Errors are pele_errors worded as Mermaid words them.
mindmap_model parse_mindmap(const ustring& source, const std::optional<ustring>& title);

rendered render_mindmap(const mindmap_model& model, const config& settings, const render_options& options);

}  // namespace pele::mindmap
