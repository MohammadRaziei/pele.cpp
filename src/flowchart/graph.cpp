#include "graph.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "chars.h"

namespace pele::flowchart {
namespace {

std::string shape_of(const flow_node& vertex) {
  if (vertex.img && !vertex.img->empty()) return "imageSquare";
  if (vertex.icon && !vertex.icon->empty()) {
    if (vertex.form && *vertex.form == u"circle") return "iconCircle";
    if (vertex.form && *vertex.form == u"square") return "iconSquare";
    if (vertex.form && *vertex.form == u"rounded") return "iconRounded";
    return "icon";
  }
  if (!vertex.type || *vertex.type == "square") return "squareRect";
  if (*vertex.type == "round") return "roundedRect";
  return *vertex.type;
}

void arrows(const std::optional<std::string>& type, std::string& start, std::string& end) {
  start = "none";
  end = "arrow_point";
  if (!type) return;
  const std::string& t = *type;
  if (t == "arrow_point" || t == "arrow_circle" || t == "arrow_cross") {
    end = t;
  } else if (t == "double_arrow_point" || t == "double_arrow_circle" || t == "double_arrow_cross") {
    const std::string single = t.substr(7);
    start = single;
    end = single;
  }
}

bool is_collapsed(const flow_subgraph& sg) {
  if (!sg.has_metadata) return false;
  const yaml_value* v = sg.metadata.find("view");
  return v != nullptr && v->is_string() && v->as_string() == u"collapsed";
}

ustring join_space(const std::vector<ustring>& parts) {
  ustring out;
  for (std::size_t i = 0; i < parts.size(); i++) {
    if (i > 0) out += u' ';
    out += parts[i];
  }
  return out;
}

}  // namespace

flow_graph build_flow_graph(const flow_db& model, const std::optional<std::string>& default_curve) {
  flow_graph graph;
  const std::vector<flow_subgraph>& subgraphs = model.subgraphs;

  auto compiled = [&](const std::vector<ustring>& class_names) {
    std::vector<ustring> out;
    for (const ustring& name : class_names) {
      const flow_class_def* def = model.find_class(name);
      if (def == nullptr) continue;
      for (const ustring& s : def->styles) out.push_back(trim(s));
      for (const ustring& s : def->text_styles) out.push_back(trim(s));
      for (ustring& s : out) s = trim(s);
    }
    return out;
  };

  std::unordered_map<ustring, ustring> subgraph_parent;
  for (const flow_subgraph& sg : subgraphs)
    for (const ustring& child : sg.nodes)
      if (model.subgraph(child) != nullptr) subgraph_parent[child] = sg.id;

  std::unordered_map<ustring, int> declaration_index;
  std::unordered_map<ustring, std::vector<ustring>> children_of;
  for (const flow_subgraph& sg : subgraphs) {
    const auto parent = subgraph_parent.find(sg.id);
    if (parent == subgraph_parent.end()) continue;
    children_of[parent->second].push_back(sg.id);
  }
  int next_index = 0;
  for (const flow_subgraph& sg : subgraphs) {
    if (subgraph_parent.count(sg.id)) continue;
    std::vector<ustring> pending = {sg.id};
    while (!pending.empty()) {
      const ustring id = pending.back();
      pending.pop_back();
      if (declaration_index.count(id)) continue;
      declaration_index[id] = next_index++;
      const auto kids = children_of.find(id);
      if (kids != children_of.end())
        for (std::size_t i = kids->second.size(); i-- > 0;) pending.push_back(kids->second[i]);
    }
  }

  auto collapsed_id = [&](const ustring& id) {
    const flow_subgraph* sg = model.subgraph(id);
    return sg != nullptr && is_collapsed(*sg);
  };
  bool any_collapsed = false;
  for (const flow_subgraph& sg : subgraphs)
    if (is_collapsed(sg)) any_collapsed = true;

  // The outermost collapsed subgraph around each subgraph, resolved once per chain of parents.
  std::unordered_map<ustring, std::optional<ustring>> outermost;
  auto outermost_collapsed = [&](const ustring& id) -> std::optional<ustring> {
    std::vector<ustring> chain;
    std::unordered_set<ustring> seen;
    std::optional<ustring> current = id;
    while (current && !outermost.count(*current) && !seen.count(*current)) {
      seen.insert(*current);
      chain.push_back(*current);
      const auto parent = subgraph_parent.find(*current);
      current = parent == subgraph_parent.end() ? std::nullopt : std::optional<ustring>(parent->second);
    }
    std::optional<ustring> result;
    if (current) {
      const auto found = outermost.find(*current);
      if (found != outermost.end()) result = found->second;
    }
    for (std::size_t i = chain.size(); i-- > 0;) {
      if (!result && collapsed_id(chain[i])) result = chain[i];
      outermost[chain[i]] = result;
    }
    return result;
  };

  std::unordered_set<ustring> hidden;
  std::unordered_map<ustring, ustring> collapsed_into;
  if (any_collapsed) {
    for (const flow_subgraph& sg : subgraphs) {
      const std::optional<ustring> ancestor = outermost_collapsed(sg.id);
      if (!ancestor) continue;
      if (sg.id != *ancestor) {
        hidden.insert(sg.id);
        collapsed_into[sg.id] = *ancestor;
      }
      for (const ustring& child : sg.nodes) {
        if (child == *ancestor) continue;
        hidden.insert(child);
        collapsed_into[child] = *ancestor;
      }
    }
  }

  std::unordered_map<ustring, ustring> parent_of;
  std::unordered_set<ustring> groups;
  for (std::size_t i = subgraphs.size(); i-- > 0;) {
    const flow_subgraph& sg = subgraphs[i];
    if (hidden.count(sg.id)) continue;
    if (!sg.nodes.empty()) groups.insert(sg.id);
    for (const ustring& id : sg.nodes) parent_of[id] = sg.id;
  }

  std::unordered_map<ustring, std::size_t> by_id;
  for (std::size_t i = subgraphs.size(); i-- > 0;) {
    const flow_subgraph& sg = subgraphs[i];
    if (hidden.count(sg.id)) continue;
    const bool collapsed = is_collapsed(sg);
    graph_node node;
    node.id = sg.id;
    node.label = sg.title;
    node.label_kind = sg.label;
    const auto parent = parent_of.find(sg.id);
    if (parent != parent_of.end()) node.parent_id = parent->second;
    node.css_compiled_styles = compiled(sg.classes);
    node.css_classes = join_space(sg.classes);
    node.shape = collapsed ? "collapsedGroup" : "rect";
    node.dir = sg.dir;
    node.is_group = !collapsed;
    const auto index = declaration_index.find(sg.id);
    if (index != declaration_index.end()) node.color_index = index->second;
    if (!collapsed && sg.has_metadata) {
      node.metadata = sg.metadata;
      node.has_metadata = true;
    }
    graph.nodes.push_back(std::move(node));
    by_id[sg.id] = graph.nodes.size() - 1;
  }

  for (const flow_node& vertex : model.nodes()) {
    if (hidden.count(vertex.id)) continue;
    const auto existing = by_id.find(vertex.id);
    if (existing != by_id.end()) {
      graph_node& node = graph.nodes[existing->second];
      node.css_styles = vertex.styles;
      node.css_compiled_styles = compiled(vertex.classes);
      node.css_classes = join_space(vertex.classes);
      continue;
    }
    const bool is_group = groups.count(vertex.id) > 0;
    graph_node node;
    node.id = vertex.id;
    node.label = vertex.text;
    node.label_kind = vertex.label;
    const auto parent = parent_of.find(vertex.id);
    if (parent != parent_of.end()) node.parent_id = parent->second;
    node.css_styles = vertex.styles;
    std::vector<ustring> with_defaults = {u"default", u"node"};
    with_defaults.insert(with_defaults.end(), vertex.classes.begin(), vertex.classes.end());
    node.css_compiled_styles = compiled(with_defaults);
    node.css_classes = u"default " + join_space(vertex.classes);
    node.dir = vertex.dir;
    node.link = vertex.link;
    node.link_target = vertex.link_target;
    if (const ustring* tip = model.tooltip(vertex.id)) node.tooltip = *tip;
    node.icon = vertex.icon;
    node.pos = vertex.pos;
    node.img = vertex.img;
    node.asset_width = vertex.asset_width;
    node.asset_height = vertex.asset_height;
    node.constraint = vertex.constraint;
    node.is_group = is_group;
    node.shape = is_group ? "rect" : shape_of(vertex);
    graph.nodes.push_back(std::move(node));
    by_id[vertex.id] = graph.nodes.size() - 1;
  }

  for (std::size_t index = 0; index < model.edges.size(); index++) {
    const flow_edge& raw = model.edges[index];
    const auto cs = collapsed_into.find(raw.start);
    const auto ce = collapsed_into.find(raw.end);
    const ustring start = cs != collapsed_into.end() ? cs->second : raw.start;
    const ustring end = ce != collapsed_into.end() ? ce->second : raw.end;
    if (start == end && (cs != collapsed_into.end() || ce != collapsed_into.end())) continue;

    graph_edge edge;
    std::string arrow_start, arrow_end;
    arrows(raw.type, arrow_start, arrow_end);
    std::vector<ustring> styles;
    if (model.default_edge_style) styles = *model.default_edge_style;
    if (raw.style) styles.insert(styles.end(), raw.style->begin(), raw.style->end());
    const bool plain = (raw.stroke && *raw.stroke == "invisible") || (raw.type && *raw.type == "arrow_open");
    edge.id = raw.id && !raw.id->empty() ? *raw.id : u"L_" + start + u"_" + end + u"_" + ascii(std::to_string(index));
    edge.is_user_defined_id = raw.is_user_defined_id;
    edge.start = start;
    edge.end = end;
    edge.type = raw.type ? *raw.type : "normal";
    edge.label = raw.text;
    edge.label_kind = raw.label;
    edge.thickness = raw.stroke;
    edge.pattern = raw.stroke;
    edge.minlen = raw.length;
    edge.classes = raw.stroke && *raw.stroke == "invisible" ? ustring() : ustring(u"edge-thickness-normal edge-pattern-solid flowchart-link");
    edge.arrow_type_start = plain ? "none" : arrow_start;
    edge.arrow_type_end = plain ? "none" : arrow_end;
    edge.css_compiled_styles = compiled(raw.classes);
    edge.style = std::move(styles);
    edge.animate = raw.animate;
    edge.animation = raw.animation;
    if (raw.interpolate && !raw.interpolate->empty()) edge.curve = raw.interpolate;
    else if (model.default_interpolate && !model.default_interpolate->empty()) edge.curve = model.default_interpolate;
    else edge.curve = default_curve;
    graph.edges.push_back(std::move(edge));
  }
  return graph;
}

}  // namespace pele::flowchart
