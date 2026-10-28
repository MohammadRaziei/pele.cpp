#include "class/graph.h"

#include <string>

namespace pele::classdiagram {
namespace {

end_marker marker_of(int type) {
  switch (type) {
    case kEndAggregation: return end_marker::aggregation;
    case kEndExtension: return end_marker::extension;
    case kEndComposition: return end_marker::composition;
    case kEndDependency: return end_marker::dependency;
    case kEndLollipop: return end_marker::lollipop;
    default: return end_marker::none;
  }
}

ustring ascii_number(int n) {
  const std::string s = std::to_string(n);
  return ustring(s.begin(), s.end());
}

}  // namespace

class_graph build_class_graph(const class_model& model, bool hierarchical) {
  class_graph graph;
  std::vector<graph_node>& nodes = graph.nodes;
  std::map<ustring, int> namespace_at;
  std::map<ustring, int> class_at;
  std::map<ustring, int> interface_at;

  std::map<ustring, std::optional<ustring>> declared;
  auto declared_ancestor = [&](const std::optional<ustring>& id) -> std::optional<ustring> {
    if (hierarchical || !id) return id;
    std::vector<ustring> chain;
    std::optional<ustring> current = id;
    std::optional<ustring> found;
    while (current && !current->empty()) {
      const auto seen = declared.find(*current);
      if (seen != declared.end()) {
        found = seen->second;
        break;
      }
      const auto ns = model.namespace_index.find(*current);
      if (ns == model.namespace_index.end()) break;
      const namespace_node& node = model.namespaces[ns->second];
      if (node.explicit_) {
        found = current;
        break;
      }
      chain.push_back(*current);
      current = node.parent;
    }
    for (const ustring& visited : chain) declared[visited] = found;
    return found;
  };
  auto group = [&](const std::optional<ustring>& id) -> int {
    if (!id) return -1;
    const auto it = namespace_at.find(*id);
    return it == namespace_at.end() ? -1 : it->second;
  };

  for (const namespace_node& ns : model.namespaces) {
    if (!hierarchical && !ns.explicit_) continue;
    namespace_at[ns.id] = static_cast<int>(nodes.size());
    graph_node node;
    node.id = ns.id;
    node.label = hierarchical ? ns.label : ns.id;
    node.is_group = true;
    node.shape = node_shape::rect;
    if (hierarchical) node.parent_id = ns.parent;
    nodes.push_back(std::move(node));
  }
  for (graph_node& node : nodes) node.parent = group(node.parent_id);

  int color_index = 0;
  for (const class_node& cls : model.classes) {
    const std::optional<ustring> parent_id = declared_ancestor(cls.parent);
    class_at[cls.id] = static_cast<int>(nodes.size());
    graph_node node;
    node.id = cls.id;
    node.label = cls.label;
    node.shape = node_shape::class_box;
    node.parent_id = parent_id;
    node.parent = group(parent_id);
    node.cls = &cls;
    node.color_index = color_index++;
    nodes.push_back(std::move(node));
  }

  for (const class_note& note : model.notes) {
    const std::optional<ustring> parent_id = declared_ancestor(note.parent);
    int target = -1;
    if (note.cls) {
      const auto it = class_at.find(*note.cls);
      if (it != class_at.end()) target = it->second;
    }
    if (target >= 0) {
      graph_edge edge;
      edge.id = u"edgeNote" + ascii_number(note.index);
      edge.start = note.id;
      edge.end = nodes[target].id;
      edge.source = static_cast<int>(nodes.size());
      edge.target = target;
      edge.pattern = edge_pattern::dotted;
      graph.edges.push_back(std::move(edge));
    }
    graph_node node;
    node.id = note.id;
    node.label = note.text;
    node.shape = node_shape::note;
    node.parent_id = parent_id;
    node.parent = group(parent_id);
    nodes.push_back(std::move(node));
  }

  // Mermaid leaves every lollipop interface outside the namespaces. Here it stays beside its class.
  for (const class_interface& item : model.interfaces) {
    const auto owner = class_at.find(item.class_id);
    interface_at[item.id] = static_cast<int>(nodes.size());
    graph_node node;
    node.id = item.id;
    node.label = item.label;
    node.shape = node_shape::interface;
    node.parent = owner == class_at.end() ? -1 : nodes[owner->second].parent;
    nodes.push_back(std::move(node));
  }

  int count = 0;
  for (const class_relation& relation : model.relations) {
    count++;
    const int type1 = relation.type1;
    const int type2 = relation.type2;
    // addRelation swaps in an interface for the lollipop end of a relation with no other marker.
    const bool start_is_interface = type1 == kEndLollipop && type2 == kEndNone;
    const bool end_is_interface = !start_is_interface && type2 == kEndLollipop && type1 == kEndNone;
    const std::map<ustring, int>& start_at = start_is_interface ? interface_at : class_at;
    const std::map<ustring, int>& end_at = end_is_interface ? interface_at : class_at;
    graph_edge edge;
    edge.id = u"id_" + relation.id1 + u"_" + relation.id2 + u"_" + ascii_number(count);
    edge.start = relation.id1;
    edge.end = relation.id2;
    const auto s = start_at.find(relation.id1);
    const auto e = end_at.find(relation.id2);
    edge.source = s == start_at.end() ? -1 : s->second;
    edge.target = e == end_at.end() ? -1 : e->second;
    // An absent title is drawn as no text.
    if (relation.title) edge.label = *relation.title;
    edge.arrow_start = marker_of(type1);
    edge.arrow_end = marker_of(type2);
    edge.start_label_right = relation.relation_title1 == u"none" ? ustring() : relation.relation_title1;
    edge.end_label_left = relation.relation_title2 == u"none" ? ustring() : relation.relation_title2;
    edge.pattern = relation.line_type == 1 ? edge_pattern::dashed : edge_pattern::solid;
    graph.edges.push_back(std::move(edge));
  }
  return graph;
}

}  // namespace pele::classdiagram
