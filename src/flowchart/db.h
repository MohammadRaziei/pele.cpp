#pragma once

#include <deque>
#include <functional>
#include <optional>

#include "model.h"

namespace pele::flowchart {

struct flow_db_options {
  std::optional<double> max_edges;
  bool inherit_dir = false;
  std::optional<std::string> direction;
  std::function<void(const std::string&)> warn;
};

// What a flowchart's text adds up to: its nodes, edges, subgraphs and classes, kept in the order
// they were declared, since that order decides how the diagram is laid out.
struct vertex_args {
  std::optional<flow_text> text;
  std::optional<std::string> type;
  std::optional<std::vector<ustring>> style;
  std::optional<std::vector<ustring>> classes;
  std::optional<std::string> dir;
  std::vector<std::pair<ustring, ustring>> props;
  bool has_props = false;
  std::optional<ustring> metadata;
};

class flow_db {
 public:
  explicit flow_db(flow_db_options options = {}) : options_(std::move(options)) {}

  std::optional<std::string> direction;
  std::vector<flow_edge> edges;
  std::vector<flow_subgraph> subgraphs;
  std::optional<std::vector<ustring>> default_edge_style;
  std::optional<std::string> default_interpolate;
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;

  // Nodes in the order they were first seen.
  const std::deque<flow_node>& nodes() const { return nodes_; }
  const flow_node* find_node(const ustring& id) const;
  const flow_class_def* find_class(const ustring& id) const;
  const ustring* tooltip(const ustring& id) const;
  const std::map<ustring, flow_class_def>& classes() const { return classes_; }
  const std::unordered_map<ustring, ustring>& tooltips() const { return tooltips_; }
  const flow_subgraph* subgraph(const ustring& id) const;


  void add_vertex(const ustring& id, vertex_args args = vertex_args());
  void add_link(const std::vector<ustring>& starts, const std::vector<ustring>& ends, const std::optional<link_info>& link);
  void update_link_interpolate(const std::vector<ustring>& positions, const std::string& interpolate);
  void update_link(const std::vector<ustring>& positions, std::vector<ustring> style);
  void add_class(const ustring& ids, const std::vector<ustring>& styles);
  void set_direction(const ustring& dir);
  void set_class(const ustring& ids, const ustring& class_name);
  void set_tooltip(const ustring& ids, const std::optional<ustring>& tooltip);
  void set_link(const ustring& ids, const ustring& link, const std::optional<ustring>& target = std::nullopt);
  void set_click_event(const ustring& ids);
  void set_acc_title(const ustring& text);
  void set_acc_description(const ustring& text);
  // `title_is_id` is true where the title is the id's own text, as for a subgraph written without `[title]`.
  ustring add_sub_graph(const std::optional<flow_text>& id_text, const std::vector<doc_item>& list, const std::optional<flow_text>& title_text, bool title_is_id);

  static link_info destruct_link(const ustring& end, const std::optional<ustring>& start = std::nullopt);

 private:
  void add_single_link(const ustring& start, const ustring& end, const std::optional<link_info>& type, const std::optional<ustring>& id);
  flow_edge& edge_at(const ustring& pos);
  flow_node* node(const ustring& id);

  std::deque<flow_node> nodes_;
  std::unordered_map<ustring, std::size_t> node_index_;
  std::map<ustring, flow_class_def> classes_;
  std::unordered_map<ustring, ustring> tooltips_;
  std::unordered_map<ustring, std::size_t> subgraph_index_;
  std::unordered_set<ustring> members_;
  std::unordered_map<ustring, std::size_t> edge_by_id_;
  std::unordered_map<ustring, int> pair_count_;
  int sub_count_ = 0;
  flow_db_options options_;
};

}  // namespace pele::flowchart
