#include "class/db.h"

#include <algorithm>

#include "entities.h"
#include "regex.h"
#include "url.h"

namespace pele::classdiagram {
namespace {

struct split_name {
  ustring class_name;
  ustring type;
};

split_name split_class_name(const ustring& id) {
  const std::size_t at = id.find(u'~');
  if (at == ustring::npos || at == 0) return {id, ustring()};
  const std::size_t end = id.find(u'~', at + 1);
  return {id.substr(0, at), id.substr(at + 1, end == ustring::npos ? ustring::npos : end - at - 1)};
}

// Mermaid looks up ids that start with a digit under this prefix, so they never match a class.
ustring lookup_id(const ustring& id) {
  const int c = id.empty() ? -1 : id[0];
  return split_class_name(c >= 48 && c <= 57 ? ustring(u"classId-") + id : id).class_name;
}

std::vector<ustring> split(const ustring& s, char16_t sep) {
  std::vector<ustring> out;
  std::size_t from = 0;
  for (;;) {
    const std::size_t at = s.find(sep, from);
    out.push_back(s.substr(from, at == ustring::npos ? ustring::npos : at - from));
    if (at == ustring::npos) break;
    from = at + 1;
  }
  return out;
}

bool contains(const ustring& s, const char16_t* part) { return s.find(part) != ustring::npos; }

}  // namespace

int class_db::find_class(const ustring& name) const {
  const auto it = model.class_index.find(name);
  return it == model.class_index.end() ? -1 : it->second;
}

void class_db::add_class(const ustring& id) {
  const split_name s = split_class_name(id);
  if (model.class_index.count(s.class_name)) return;
  class_node node;
  node.id = s.class_name;
  node.type = s.type;
  node.label = s.class_name;
  node.css_classes = u"default";
  const int index = static_cast<int>(model.classes.size());
  model.classes.push_back(std::move(node));
  model.class_index[s.class_name] = index;
  tag(index, u"default");
}

void class_db::tag(int node, const ustring& css_class) {
  std::vector<int>& tagged = with_css_class_[css_class];
  if (std::find(tagged.begin(), tagged.end(), node) == tagged.end()) tagged.push_back(node);
}

// Returns the class for a name that may carry a generic type, creating it if needed.
// Mermaid throws a TypeError where this creates, as for an annotation on a class not yet declared.
int class_db::ensure(const ustring& id) {
  add_class(id);
  return find_class(split_class_name(id).class_name);
}

void class_db::set_class_label(const ustring& id, const ustring& label) { model.classes[static_cast<std::size_t>(ensure(id))].label = label; }

void class_db::add_relation(class_relation relation) {
  if (relation.type1 == kEndLollipop && relation.type2 == kEndNone) {
    add_class(relation.id2);
    const ustring id = u"interface" + to_utf16(std::to_string(model.interfaces.size()));
    model.interfaces.push_back({id, relation.id1, relation.id2});
    relation.id1 = id;
  } else if (relation.type2 == kEndLollipop && relation.type1 == kEndNone) {
    add_class(relation.id1);
    const ustring id = u"interface" + to_utf16(std::to_string(model.interfaces.size()));
    model.interfaces.push_back({id, relation.id2, relation.id1});
    relation.id2 = id;
  } else {
    add_class(relation.id1);
    add_class(relation.id2);
  }
  relation.id1 = split_class_name(relation.id1).class_name;
  relation.id2 = split_class_name(relation.id2).class_name;
  relation.relation_title1 = trim(relation.relation_title1);
  relation.relation_title2 = trim(relation.relation_title2);
  model.relations.push_back(std::move(relation));
}

void class_db::add_annotation(const ustring& class_name, const ustring& annotation) {
  model.classes[static_cast<std::size_t>(ensure(class_name))].annotations.push_back(annotation);
}

void class_db::add_member(const ustring& class_name, const ustring& member) {
  class_node& node = model.classes[static_cast<std::size_t>(ensure(class_name))];
  const ustring text = trim(member);
  if (text.rfind(u"<<", 0) == 0 && text.size() >= 2 && text.compare(text.size() - 2, 2, u">>") == 0) {
    // `text.substring(2, text.length - 2)`
    const std::size_t a = 2, b = text.size() >= 2 ? text.size() - 2 : 0;
    node.annotations.push_back(a <= b ? text.substr(a, b - a) : text.substr(b, a - b));
  } else if (text.find(u')') != ustring::npos && text.find(u')') > 0) {
    class_member parsed(text, class_member::kind::method);
    (parsed.member_type == class_member::kind::method ? node.methods : node.members).push_back(std::move(parsed));
  } else if (!text.empty()) {
    node.members.push_back(class_member(text, class_member::kind::attribute));
  }
}

void class_db::add_members(const ustring& class_name, std::vector<ustring> members) {
  std::reverse(members.begin(), members.end());
  for (const ustring& member : members) add_member(class_name, member);
}

ustring class_db::add_note(const ustring& text, const std::optional<ustring>& class_name) {
  const int index = static_cast<int>(model.notes.size());
  const ustring id = u"note" + to_utf16(std::to_string(index));
  class_note note;
  note.id = id;
  note.cls = class_name;
  note.text = text;
  note.index = index;
  // `this.notes.set(id, ...)`: an id that exists is replaced where it is, which `size` never allows.
  model.notes.push_back(std::move(note));
  model.note_index[id] = index;
  return id;
}

ustring class_db::cleanup_label(const ustring& label) { return trim(!label.empty() && label[0] == u':' ? label.substr(1) : label); }

void class_db::set_css_class(const ustring& ids, const ustring& class_name) {
  for (const ustring& id : split(ids, u',')) {
    const int node = find_class(lookup_id(id));
    if (node < 0) continue;
    model.classes[static_cast<std::size_t>(node)].css_classes += u" " + class_name;
    for (const ustring& word : split(class_name, u' ')) tag(node, word);
  }
}

void class_db::define_class(const std::vector<ustring>& ids, const std::vector<ustring>& style) {
  for (const ustring& id : ids) {
    int index;
    const auto it = model.style_class_index.find(id);
    if (it == model.style_class_index.end()) {
      index = static_cast<int>(model.style_classes.size());
      model.style_classes.push_back({id, {}, {}});
      model.style_class_index[id] = index;
    } else {
      index = it->second;
    }
    style_class& sc = model.style_classes[static_cast<std::size_t>(index)];
    for (const ustring& s : style) {
      if (contains(s, u"color")) {
        // `s.replace('fill', 'bgFill')`: the first one only.
        ustring replaced = s;
        const std::size_t at = replaced.find(u"fill");
        if (at != ustring::npos) replaced.replace(at, 4, u"bgFill");
        sc.text_styles.push_back(replaced);
      }
      sc.styles.push_back(s);
    }
    // Mermaid tests `cssClasses.includes(id)`, which also matches part of a longer name
    // (a classDef named `def` styles every class, through `default`). Whole names are compared here.
    const auto tagged = with_css_class_.find(id);
    if (tagged == with_css_class_.end()) continue;
    std::vector<ustring> parts;
    for (const ustring& s : style)
      for (ustring& part : split(s, u',')) parts.push_back(std::move(part));
    for (int node : tagged->second)
      for (const ustring& part : parts) model.classes[static_cast<std::size_t>(node)].styles.push_back(part);
  }
}

void class_db::set_tooltip(const ustring& ids, const std::optional<ustring>& tooltip) {
  if (!tooltip) return;
  for (const ustring& id : split(ids, u',')) {
    const int node = find_class(split_class_name(id).class_name);
    if (node >= 0) model.classes[static_cast<std::size_t>(node)].tooltip = *tooltip;
  }
}

void class_db::set_link(const ustring& ids, const ustring& link, const std::optional<ustring>& target) {
  // Entity codes are resolved first, so that what is checked is what a browser would follow.
  const ustring url = trim(decode_entities(link));
  for (const ustring& id : split(ids, u',')) {
    const int node = find_class(lookup_id(id));
    if (node >= 0) {
      class_node& n = model.classes[static_cast<std::size_t>(node)];
      n.link = url.empty() ? std::nullopt : std::optional<ustring>(sanitize_url(url));
      n.link_target = target ? *target : ustring(u"_blank");
    }
  }
  set_css_class(ids, u"clickable");
}

// Callbacks are never executed; the class is only marked clickable, as in Mermaid's strict mode.
void class_db::set_click_event(const ustring& ids) {
  for (const ustring& id : split(ids, u',')) {
    const int node = find_class(split_class_name(id).class_name);
    if (node >= 0) model.classes[static_cast<std::size_t>(node)].have_callback = true;
  }
  set_css_class(ids, u"clickable");
}

void class_db::set_acc_title(const ustring& text) { model.acc_title = trim_start(text); }

void class_db::set_acc_description(const ustring& text) {
  static const js_regex line_indent(R"(\n\s+)");
  model.acc_descr = line_indent.replace_all(text, ustring_view(u"\n"));
}

ustring class_db::add_namespace(const ustring& id, const std::optional<ustring>& label) {
  const ustring prefix = namespace_stack_.empty() ? ustring() : namespace_stack_.back();
  const ustring qualified = !prefix.empty() ? prefix + u"." + id : id;
  namespace_stack_.push_back(qualified);

  const auto existing = model.namespace_index.find(qualified);
  if (existing != model.namespace_index.end()) {
    namespace_node& node = model.namespaces[static_cast<std::size_t>(existing->second)];
    node.explicit_ = true;
    if (label && !label->empty()) node.label = *label;
    return qualified;
  }

  // The enclosing namespace and its ancestors exist already, so only the parts of `id` are walked.
  std::optional<ustring> parent_id = !prefix.empty() ? std::optional<ustring>(prefix) : std::nullopt;
  std::size_t from = !prefix.empty() ? prefix.size() + 1 : 0;
  for (;;) {
    const std::size_t dot = qualified.find(u'.', from);
    const bool leaf = dot == ustring::npos;
    const ustring current = leaf ? qualified : qualified.substr(0, dot);
    int index;
    const auto found = model.namespace_index.find(current);
    if (found == model.namespace_index.end()) {
      const ustring part = qualified.substr(from, leaf ? ustring::npos : dot - from);
      namespace_node node;
      node.id = current;
      node.label = leaf && label && !label->empty() ? *label : part;
      node.parent = parent_id;
      node.explicit_ = leaf;
      index = static_cast<int>(model.namespaces.size());
      model.namespaces.push_back(std::move(node));
      model.namespace_index[current] = index;
    } else {
      index = found->second;
      if (leaf) model.namespaces[static_cast<std::size_t>(index)].explicit_ = true;
    }
    if (parent_id && !parent_id->empty()) {
      const auto parent = model.namespace_index.find(*parent_id);
      if (parent != model.namespace_index.end()) {
        std::vector<int>& children = model.namespaces[static_cast<std::size_t>(parent->second)].children;
        if (std::find(children.begin(), children.end(), index) == children.end()) children.push_back(index);
        namespace_node& node = model.namespaces[static_cast<std::size_t>(index)];
        if (!node.parent) node.parent = parent_id;
      }
    }
    if (leaf) return qualified;
    parent_id = current;
    from = dot + 1;
  }
}

void class_db::add_classes_to_namespace(const ustring& id, const std::vector<ustring>& class_names, const std::vector<ustring>& note_names) {
  const auto found = model.namespace_index.find(id);
  if (found == model.namespace_index.end()) return;
  namespace_node& ns = model.namespaces[static_cast<std::size_t>(found->second)];
  for (const ustring& name : class_names) {
    const int node = find_class(split_class_name(name).class_name);
    if (node < 0) continue;
    model.classes[static_cast<std::size_t>(node)].parent = id;
    if (std::find(ns.classes.begin(), ns.classes.end(), node) == ns.classes.end()) ns.classes.push_back(node);
  }
  for (const ustring& name : note_names) {
    const auto note = model.note_index.find(name);
    if (note == model.note_index.end()) continue;
    model.notes[static_cast<std::size_t>(note->second)].parent = id;
    if (std::find(ns.notes.begin(), ns.notes.end(), note->second) == ns.notes.end()) ns.notes.push_back(note->second);
  }
}

void class_db::set_css_style(const ustring& id, const std::vector<ustring>& styles) {
  const int node = find_class(id);
  if (node < 0) return;
  for (const ustring& s : styles)
    for (ustring& part : split(s, u',')) model.classes[static_cast<std::size_t>(node)].styles.push_back(std::move(part));
}

}  // namespace pele::classdiagram
