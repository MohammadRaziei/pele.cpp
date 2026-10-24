#pragma once

#include <optional>
#include <vector>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::kanban {

struct kanban_node {
  ustring id;
  int level = 0;
  ustring label;
  std::optional<ustring> icon;
  std::optional<ustring> assigned;
  std::optional<ustring> ticket;
  std::optional<ustring> priority;
  std::optional<ustring> css_classes;
};

// A column: its own node, and the cards under it.
struct kanban_section : kanban_node {
  std::vector<kanban_node> items;
};

struct kanban_model {
  std::optional<ustring> title;
  std::vector<kanban_section> sections;
};

// Reads the text of a kanban diagram into a model. Errors are pele_errors worded as Mermaid words them.
kanban_model parse_kanban(const ustring& source, const std::optional<ustring>& title);

rendered render_kanban(const kanban_model& model, const config& settings, const render_options& options);

}  // namespace pele::kanban
