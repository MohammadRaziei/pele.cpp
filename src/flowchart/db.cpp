#include "db.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "chars.h"
#include "errors.h"
#include "js_math.h"
#include "shape_names.h"
#include "url.h"

namespace pele::flowchart {
namespace {

label_type label_type_of(const std::optional<label_type>& type) { return type ? *type : label_type::markdown; }

ustring unquote(const ustring& text) {
  if (text.size() >= 1 && text.front() == '"' && text.back() == '"') {
    // startsWith and endsWith both hold for a lone quote, whose substring is empty.
    return text.size() >= 2 ? text.substr(1, text.size() - 2) : ustring();
  }
  return text;
}

int count_char(char16_t ch, const ustring& s) { return static_cast<int>(std::count(s.begin(), s.end(), ch)); }

struct start_info {
  std::string type;
  std::string stroke;
  std::optional<double> length;
};

start_info destruct_start_link(const ustring& raw) {
  ustring str = trim(raw);
  std::string type = "arrow_open";
  if (!str.empty()) {
    if (str[0] == '<') {
      type = "arrow_point";
      str = str.substr(1);
    } else if (str[0] == 'x') {
      type = "arrow_cross";
      str = str.substr(1);
    } else if (str[0] == 'o') {
      type = "arrow_circle";
      str = str.substr(1);
    }
  }
  std::string stroke = "normal";
  if (str.find(u'=') != ustring::npos) stroke = "thick";
  if (str.find(u'.') != ustring::npos) stroke = "dotted";
  return {type, stroke, std::nullopt};
}

struct end_info {
  std::string type;
  std::string stroke;
  double length;
};

end_info destruct_end_link(const ustring& raw) {
  const ustring str = trim(raw);
  ustring line = str.empty() ? ustring() : str.substr(0, str.size() - 1);
  std::string type = "arrow_open";
  const int last = str.empty() ? -1 : str.back();
  if (last == 'x') {
    type = "arrow_cross";
    if (!str.empty() && str[0] == 'x') {
      type = "double_" + type;
      line = line.empty() ? line : line.substr(1);
    }
  } else if (last == '>') {
    type = "arrow_point";
    if (!str.empty() && str[0] == '<') {
      type = "double_" + type;
      line = line.empty() ? line : line.substr(1);
    }
  } else if (last == 'o') {
    type = "arrow_circle";
    if (!str.empty() && str[0] == 'o') {
      type = "double_" + type;
      line = line.empty() ? line : line.substr(1);
    }
  }
  std::string stroke = "normal";
  double length = static_cast<double>(line.size()) - 1;
  if (!line.empty() && line[0] == '=') stroke = "thick";
  if (!line.empty() && line[0] == '~') stroke = "invisible";
  const int dots = count_char('.', line);
  if (dots) {
    stroke = "dotted";
    length = dots;
  }
  return {type, stroke, length};
}

std::vector<ustring> split(const ustring& s, char16_t sep) {
  std::vector<ustring> out;
  std::size_t from = 0;
  for (;;) {
    const std::size_t at = s.find(sep, from);
    if (at == ustring::npos) {
      out.push_back(s.substr(from));
      return out;
    }
    out.push_back(s.substr(from, at - from));
    from = at + 1;
  }
}

bool has_upper_ascii(const ustring& s) {
  for (char16_t c : s)
    if (c >= 'A' && c <= 'Z') return true;
  return false;
}

// Number(value) for a metadata value.
double to_number(const yaml_value& v) {
  switch (v.type()) {
    case yaml_value::kind::number: return v.as_number();
    case yaml_value::kind::boolean: return v.as_boolean() ? 1 : 0;
    case yaml_value::kind::string: {
      const std::string t = to_utf8(trim(v.as_string()));
      if (t.empty()) return 0;
      char* end = nullptr;
      const double d = std::strtod(t.c_str(), &end);
      return *end == '\0' ? d : std::nan("");
    }
    case yaml_value::kind::null: return 0;
    default: return std::nan("");
  }
}

std::string to_ascii(const yaml_value& v) { return to_utf8(v.to_js_string()); }

pele_error flow_error(const std::string& message, error_code code) {
  pele_error::info info;
  info.type = "flowchart";
  return pele_error(message, code, std::move(info));
}

}  // namespace

const flow_node* flow_db::find_node(const ustring& id) const {
  const auto it = node_index_.find(id);
  return it == node_index_.end() ? nullptr : &nodes_[it->second];
}

flow_node* flow_db::node(const ustring& id) {
  const auto it = node_index_.find(id);
  return it == node_index_.end() ? nullptr : &nodes_[it->second];
}

const flow_class_def* flow_db::find_class(const ustring& id) const {
  const auto it = classes_.find(id);
  return it == classes_.end() ? nullptr : &it->second;
}

const ustring* flow_db::tooltip(const ustring& id) const {
  const auto it = tooltips_.find(id);
  return it == tooltips_.end() ? nullptr : &it->second;
}

const flow_subgraph* flow_db::subgraph(const ustring& id) const {
  const auto it = subgraph_index_.find(id);
  return it == subgraph_index_.end() ? nullptr : &subgraphs[it->second];
}

void flow_db::add_vertex(const ustring& id, vertex_args args) {
  if (id.empty() || trim(id).empty()) return;

  std::optional<yaml_value> doc;
  if (args.metadata) {
    const ustring& metadata = *args.metadata;
    const ustring text = metadata.find(u'\n') != ustring::npos ? metadata + u"\n" : u"{\n" + metadata + u"\n}";
    yaml_value parsed;
    try {
      parsed = parse_yaml(text);
    } catch (const yaml_error& error) {
      throw flow_error("Metadata of \"" + to_utf8(id) + "\" is not valid YAML. " + error.what(), error_code::syntax);
    }
    if (parsed.is_object()) doc = std::move(parsed);
  }

  const auto sg = subgraph_index_.find(id);
  if (sg != subgraph_index_.end() && doc) {
    flow_subgraph& target = subgraphs[sg->second];
    yaml_value merged = target.has_metadata ? target.metadata : yaml_value::object();
    for (const ustring& key : doc->keys()) merged.set(key, *doc->find(key));
    target.metadata = std::move(merged);
    target.has_metadata = true;
    return;
  }

  const auto edge_it = edge_by_id_.find(id);
  if (edge_it != edge_by_id_.end()) {
    flow_edge& edge = edges[edge_it->second];
    if (doc) {
      if (const yaml_value* v = doc->find("animate")) edge.animate = *v;
      if (const yaml_value* v = doc->find("animation")) edge.animation = *v;
      if (const yaml_value* v = doc->find("curve")) edge.interpolate = to_ascii(*v);
    }
    return;
  }

  flow_node* vertex = node(id);
  if (vertex == nullptr) {
    if (!args.text && !args.type && args.style && options_.warn)
      options_.warn("Style applied to unknown node \"" + to_utf8(id) + "\". This may indicate a typo. The node will be created automatically.");
    nodes_.emplace_back();
    nodes_.back().id = id;
    node_index_[id] = nodes_.size() - 1;
    vertex = &nodes_.back();
  }

  if (args.text) {
    vertex->label = args.text->type;
    vertex->text = unquote(trim(args.text->text));
  } else if (!vertex->text) {
    vertex->text = id;
  }
  if (args.type) vertex->type = args.type;
  if (args.style)
    for (const ustring& s : *args.style) vertex->styles.push_back(s);
  if (args.classes)
    for (const ustring& c : *args.classes) vertex->classes.push_back(c);
  if (args.dir) vertex->dir = args.dir;
  for (auto& kv : args.props) {
    bool found = false;
    for (auto& existing : vertex->props)
      if (existing.first == kv.first) {
        existing.second = kv.second;
        found = true;
      }
    if (!found) vertex->props.push_back(kv);
  }

  if (!doc) return;

  const yaml_value* shape_v = doc->find("shape");
  if (shape_v != nullptr && shape_v->truthy()) {
    const ustring shape = shape_v->to_js_string();
    if (has_upper_ascii(shape) || shape.find(u'_') != ustring::npos)
      throw flow_error("No such shape: " + to_utf8(shape) + ". Shape names should be lowercase.", error_code::semantic);
    if (!is_valid_shape(to_utf8(shape))) throw flow_error("No such shape: " + to_utf8(shape) + ".", error_code::semantic);
    vertex->type = to_utf8(shape);
  }
  const yaml_value* label_v = doc->find("label");
  std::optional<ustring> label;
  if (label_v != nullptr && label_v->is_string()) label = label_v->as_string();
  if (label_v != nullptr && label_v->truthy()) {
    vertex->text = label_v->to_js_string();
    const yaml_value* lt = doc->find("labelType");
    std::optional<label_type> type;
    if (lt != nullptr && lt->is_string()) {
      if (lt->as_string() == u"markdown") type = label_type::markdown;
      else if (lt->as_string() == u"string") type = label_type::string;
      else if (lt->as_string() == u"text") type = label_type::text;
    }
    vertex->label = label_type_of(type);
  }
  auto empty_label = [&]() { return !label || trim(*label).empty(); };
  if (const yaml_value* v = doc->find("icon"); v != nullptr && v->truthy()) {
    vertex->icon = v->to_js_string();
    if (empty_label() && vertex->text && *vertex->text == id) vertex->text = ustring();
  }
  if (const yaml_value* v = doc->find("form"); v != nullptr && v->truthy()) vertex->form = v->to_js_string();
  if (const yaml_value* v = doc->find("pos"); v != nullptr && v->truthy()) vertex->pos = v->to_js_string();
  if (const yaml_value* v = doc->find("img"); v != nullptr && v->truthy()) {
    vertex->img = v->to_js_string();
    if (empty_label() && vertex->text && *vertex->text == id) vertex->text = ustring();
  }
  if (const yaml_value* v = doc->find("constraint"); v != nullptr && v->truthy()) vertex->constraint = v->to_js_string();
  if (const yaml_value* v = doc->find("w"); v != nullptr && v->truthy()) vertex->asset_width = to_number(*v);
  if (const yaml_value* v = doc->find("h"); v != nullptr && v->truthy()) vertex->asset_height = to_number(*v);
}

void flow_db::add_single_link(const ustring& start, const ustring& end, const std::optional<link_info>& type, const std::optional<ustring>& id) {
  flow_edge edge;
  edge.start = start;
  edge.end = end;
  edge.interpolate = default_interpolate;
  if (type && type->text) {
    edge.text = unquote(trim(type->text->text));
    edge.label = type->text->type;
  }
  if (type) {
    edge.type = type->type;
    edge.stroke = type->stroke;
    if (type->length) edge.length = *type->length > 10 ? 10 : *type->length;
  }
  if (id && !id->empty() && edge_by_id_.find(*id) == edge_by_id_.end()) {
    edge.id = *id;
    edge.is_user_defined_id = true;
  } else {
    const ustring pair = start + ustring(1, u'\0') + end;
    const auto it = pair_count_.find(pair);
    const int existing = it == pair_count_.end() ? 0 : it->second;
    edge.id = u"L_" + start + u"_" + end + u"_" + ascii(std::to_string(existing == 0 ? 0 : existing + 1));
  }

  if (options_.max_edges && static_cast<double>(edges.size()) >= *options_.max_edges) {
    throw flow_error("Edge limit exceeded. The limit is " + js_number_string(*options_.max_edges) + ".",
                     error_code::limit);
  }
  edges.push_back(edge);
  const ustring pair = start + ustring(1, u'\0') + end;
  pair_count_[pair] = pair_count_[pair] + 1;
  if (edge_by_id_.find(*edge.id) == edge_by_id_.end()) edge_by_id_[*edge.id] = edges.size() - 1;
}

void flow_db::add_link(const std::vector<ustring>& starts, const std::vector<ustring>& ends, const std::optional<link_info>& link) {
  std::optional<ustring> id;
  if (link && link->id) {
    ustring text = *link->id;
    const std::size_t at = text.find(u'@');
    if (at != ustring::npos) text.erase(at, 1);
    id = text;
  }
  const ustring& last_start = starts.back();
  const ustring& first_end = ends.front();
  for (const ustring& start : starts)
    for (const ustring& end : ends) add_single_link(start, end, link, start == last_start && end == first_end ? id : std::nullopt);
}

flow_edge& flow_db::edge_at(const ustring& pos) {
  // Number(pos): the position is a run of digits.
  double index = 0;
  for (char16_t c : pos) index = index * 10 + (c - '0');
  if (!(index >= 0 && index < static_cast<double>(edges.size())))
    throw flow_error("The index " + to_utf8(pos) + " for linkStyle is out of bounds. Valid indices for linkStyle are between 0 and " +
                         std::to_string(static_cast<long long>(edges.size()) - 1) + ". (Help: Ensure that the index is within the range of existing edges.)",
                     error_code::semantic);
  return edges[static_cast<std::size_t>(index)];
}

void flow_db::update_link_interpolate(const std::vector<ustring>& positions, const std::string& interpolate) {
  for (const ustring& pos : positions) {
    if (pos == u"default") default_interpolate = interpolate;
    else edge_at(pos).interpolate = interpolate;
  }
}

void flow_db::update_link(const std::vector<ustring>& positions, std::vector<ustring> style) {
  bool padded = false;
  for (const ustring& pos : positions) {
    if (pos == u"default") {
      default_edge_style = style;
      continue;
    }
    flow_edge& edge = edge_at(pos);
    // The style is shared by every edge it is given to, so the fill that is added to it once is on all.
    if (!padded) {
      padded = true;
      bool has_fill = false;
      for (const ustring& s : style)
        if (s.compare(0, 4, u"fill") == 0) has_fill = true;
      if (!style.empty() && !has_fill) style.push_back(u"fill:none");
    }
    edge.style = style;
  }
}

void flow_db::add_class(const ustring& ids, const std::vector<ustring>& styles) {
  ustring joined;
  for (std::size_t i = 0; i < styles.size(); i++) {
    if (i > 0) joined += u',';
    joined += styles[i];
  }
  // `\,` stays a comma; any other comma separates two declarations.
  ustring unescaped;
  for (std::size_t i = 0; i < joined.size(); i++) {
    if (joined[i] == '\\' && i + 1 < joined.size() && joined[i + 1] == ',') {
      unescaped += u'\x01';
      i++;
    } else {
      unescaped += joined[i];
    }
  }
  for (char16_t& c : unescaped)
    if (c == ',') c = ';';
  for (char16_t& c : unescaped)
    if (c == 0x01) c = ',';
  const std::vector<ustring> style = split(unescaped, ';');
  for (const ustring& id : split(ids, ',')) {
    auto it = classes_.find(id);
    if (it == classes_.end()) {
      flow_class_def def;
      def.id = id;
      it = classes_.emplace(id, std::move(def)).first;
    }
    for (const ustring& s : style) {
      if (s.find(u"color") != ustring::npos) {
        ustring text_style = s;
        const std::size_t fill = text_style.find(u"fill");
        if (fill != ustring::npos) text_style.replace(fill, 4, u"bgFill");
        it->second.text_styles.push_back(text_style);
      }
      it->second.styles.push_back(s);
    }
  }
}

void flow_db::set_direction(const ustring& dir) {
  ustring d = trim(dir);
  if (d.find(u'<') != ustring::npos) d = u"RL";
  if (d.find(u'^') != ustring::npos) d = u"BT";
  if (d.find(u'>') != ustring::npos) d = u"LR";
  if (d.find(u'v') != ustring::npos) d = u"TB";
  if (d == u"TD") d = u"TB";
  direction = to_utf8(d);
}

void flow_db::set_class(const ustring& ids, const ustring& class_name) {
  for (const ustring& id : split(ids, ',')) {
    if (flow_node* n = node(id)) n->classes.push_back(class_name);
    const auto e = edge_by_id_.find(id);
    if (e != edge_by_id_.end()) edges[e->second].classes.push_back(class_name);
    const auto s = subgraph_index_.find(id);
    if (s != subgraph_index_.end()) subgraphs[s->second].classes.push_back(class_name);
  }
}

void flow_db::set_tooltip(const ustring& ids, const std::optional<ustring>& tooltip) {
  if (!tooltip) return;
  for (const ustring& id : split(ids, ',')) tooltips_[id] = *tooltip;
}

void flow_db::set_link(const ustring& ids, const ustring& link, const std::optional<ustring>& target) {
  const ustring url = trim(link);
  for (const ustring& id : split(ids, ',')) {
    if (flow_node* vertex = node(id)) {
      vertex->link = url.empty() ? std::nullopt : std::optional<ustring>(safe_url(url));
      vertex->link_target = target;
    }
  }
  set_class(ids, u"clickable");
}

void flow_db::set_click_event(const ustring& ids) { set_class(ids, u"clickable"); }

void flow_db::set_acc_title(const ustring& text) { acc_title = trim_start(text); }

void flow_db::set_acc_description(const ustring& text) {
  ustring out;
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '\n') {
      out += u'\n';
      i++;
      while (i < text.size() && is_space(text[i])) i++;
    } else {
      out += text[i++];
    }
  }
  acc_descr = out;
}

ustring flow_db::add_sub_graph(const std::optional<flow_text>& id_text, const std::vector<doc_item>& list, const std::optional<flow_text>& title_text, bool title_is_id) {
  std::optional<ustring> id;
  if (id_text) id = trim(id_text->text);
  ustring heading = title_text ? title_text->text : ustring();
  bool has_space = false;
  for (char16_t c : heading)
    if (is_space(c)) has_space = true;
  if (id_text && title_is_id && has_space) id.reset();

  std::unordered_set<ustring> seen;
  std::vector<ustring> node_list;
  std::optional<std::string> dir;
  for (const doc_item& entry : list) {
    if (entry.what == doc_item::kind::name) {
      if (!trim(entry.name).empty() && seen.insert(entry.name).second) node_list.push_back(entry.name);
    } else if (entry.what == doc_item::kind::names) {
      for (const ustring& item : entry.names)
        if (!trim(item).empty() && seen.insert(item).second) node_list.push_back(item);
    } else {
      dir = entry.direction;
    }
  }
  if (!dir && options_.inherit_dir) dir = direction ? direction : options_.direction;

  if (!id) id = u"subGraph" + ascii(std::to_string(sub_count_));
  sub_count_++;

  flow_subgraph sub;
  sub.id = *id;
  sub.title = trim(heading);
  sub.dir = dir;
  sub.label = label_type_of(title_text ? std::optional<label_type>(title_text->type) : std::nullopt);
  // A node belongs to the first subgraph that lists it, and a subgraph never contains itself.
  for (const ustring& node_id : node_list)
    if (members_.find(node_id) == members_.end() && node_id != *id) sub.nodes.push_back(node_id);
  for (const ustring& node_id : sub.nodes) members_.insert(node_id);

  const auto existing = subgraph_index_.find(*id);
  if (existing != subgraph_index_.end()) {
    for (const ustring& node_id : sub.nodes) subgraphs[existing->second].nodes.push_back(node_id);
  } else {
    subgraphs.push_back(sub);
    subgraph_index_[*id] = subgraphs.size() - 1;
  }
  return *id;
}

link_info flow_db::destruct_link(const ustring& end, const std::optional<ustring>& start) {
  const end_info info = destruct_end_link(end);
  link_info result;
  result.type = info.type;
  result.stroke = info.stroke;
  result.length = info.length;
  if (!start || start->empty()) return result;
  start_info s = destruct_start_link(*start);
  if (s.stroke != info.stroke) return link_info{"INVALID", "INVALID", std::nullopt, std::nullopt, std::nullopt};
  if (s.type == "arrow_open") {
    s.type = info.type;
  } else {
    if (s.type != info.type) return link_info{"INVALID", "INVALID", std::nullopt, std::nullopt, std::nullopt};
    s.type = "double_" + s.type;
  }
  if (s.type == "double_arrow") s.type = "double_arrow_point";
  result.type = s.type;
  result.stroke = s.stroke;
  result.length = info.length;
  return result;
}

}  // namespace pele::flowchart
