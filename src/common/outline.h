#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ustring.h"

namespace pele::outline {

// The indented outline grammar that Mermaid's mindmap and kanban diagrams share. Kanban adds
// `@{ ... }` metadata after a node and keeps `@` out of node ids.

enum token : int {
  T_END,
  T_ERROR,
  T_SPACELINE,
  T_NL,
  T_EOF,
  T_KEYWORD,
  T_SPACELIST,
  T_ICON,
  T_CLASS,
  T_NODE_DSTART,
  T_NODE_ID,
  T_NODE_DESCR,
  T_NODE_DEND,
  T_SHAPE_DATA,
};

// As the generated parser prints them. It has no name for the end of input and prints its number.
std::string token_name(int type, bool kanban);

struct tokens {
  std::vector<int> types;
  std::vector<ustring> texts;
  std::vector<std::size_t> starts;
};

tokens tokenize(ustring_view src, bool kanban);

enum node_type : int { NT_DEFAULT = 0, NT_NO_BORDER = 0, NT_ROUNDED_RECT = 1, NT_RECT = 2, NT_CIRCLE = 3, NT_CLOUD = 4, NT_BANG = 5, NT_HEXAGON = 6 };

// Any opening delimiter pairs with any closing one, as in Mermaid.
int get_type(ustring_view start, ustring_view end);

// What the grammar's actions call, in the order the generated parser calls them.
class outline_db {
 public:
  virtual ~outline_db() = default;
  virtual int type_of(const ustring& start, const ustring& end) { return get_type(start, end); }
  virtual void add_node(int level, const ustring& id, const ustring& descr, int type, const std::optional<ustring>& shape_data) = 0;
  virtual void decorate_node(const std::optional<ustring>& icon, const std::optional<ustring>& css_class) = 0;
};

// Reads an outline, calling `db` as the grammar's actions do, and reports errors in the state
// where the generated parser would.
void parse_outline(const ustring& src, outline_db& db, bool kanban);

}  // namespace pele::outline
