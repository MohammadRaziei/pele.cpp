#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::state {

struct stmt;
using stmt_ptr = std::shared_ptr<stmt>;
using doc_ptr = std::shared_ptr<std::vector<stmt_ptr>>;

enum class stmt_kind { text, state, relation, class_def, style, apply_class, dir, click, root };

struct state_note {
  std::optional<ustring> position;
  ustring text;
};

// A description is one string or a list of them.
struct description {
  bool is_array = false;
  ustring text;
  std::vector<ustring> list;

  // JavaScript's truthiness: an empty string is false, an array is true.
  bool truthy() const { return is_array || !text.empty(); }
};

// One statement of a document. The fields each kind uses are named after Mermaid's.
struct stmt {
  stmt_kind kind = stmt_kind::text;
  // text: the text; state, classDef, style, applyClass and click: the id; dir: the direction.
  ustring text;
  // state
  std::optional<ustring> type;
  std::optional<description> descr;
  std::vector<ustring> descriptions;
  doc_ptr doc;
  std::optional<state_note> note;
  std::optional<bool> start;
  std::vector<ustring> classes;
  std::vector<ustring> styles;
  std::vector<ustring> text_styles;
  // relation
  stmt_ptr state1;
  stmt_ptr state2;
  std::optional<ustring> relation_text;
  // classDef: the styles; style and applyClass: the style or class
  ustring value;
  // click
  ustring url;
  ustring tooltip;
};

struct state_class_def {
  ustring id;
  std::vector<ustring> styles;
  std::vector<ustring> text_styles;
};

struct state_link {
  ustring url;
  ustring tooltip;
};

// A node of the graph that is drawn.
struct state_node {
  ustring id;
  // rect, rectWithTitle, stateStart, stateEnd, choice, fork, join, divider, roundedWithTitle, note or noteGroup.
  std::optional<ustring> shape;
  ustring label;
  std::vector<ustring> description;
  ustring css_classes;
  std::vector<ustring> css_compiled_styles;
  std::vector<ustring> css_styles;
  std::optional<ustring> type;
  bool is_group = false;
  std::optional<ustring> parent_id;
  std::optional<ustring> dir;
  std::optional<ustring> position;
  std::optional<ustring> note_for;
};

struct state_edge {
  ustring id;
  ustring start;
  ustring end;
  ustring label;
  ustring classes;
  bool dashed = false;
};

struct state_graph {
  std::vector<state_node> nodes;
  std::vector<state_edge> edges;
};

struct state_model {
  doc_ptr root_doc = std::make_shared<std::vector<stmt_ptr>>();
  std::map<ustring, stmt_ptr> states;
  std::map<ustring, state_class_def> classes;
  std::map<ustring, state_link> links;
  state_graph graph;
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  int divider_count = 0;

  // The direction the root document is laid out in: its last `direction` statement.
  ustring direction() const;
};

// Reads the text of a state diagram into `model`. A syntax error is a pele_error worded as Mermaid words it.
void parse_state(const ustring& source, state_model& model);

rendered render_state(const state_model& model, const config& settings, const render_options& options);

}  // namespace pele::state
