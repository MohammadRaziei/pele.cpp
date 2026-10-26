#pragma once

#include <map>
#include <string>
#include <vector>

#include "class/types.h"

namespace pele::classdiagram {

// What the grammar's actions build: the model, as Mermaid's ClassDb makes it.
class class_db {
 public:
  class_model model;

  void add_class(const ustring& id);
  void set_class_label(const ustring& id, const ustring& label);
  void add_relation(class_relation relation);
  void add_annotation(const ustring& class_name, const ustring& annotation);
  void add_member(const ustring& class_name, const ustring& member);
  void add_members(const ustring& class_name, std::vector<ustring> members);
  ustring add_note(const ustring& text, const std::optional<ustring>& class_name = std::nullopt);
  static ustring cleanup_label(const ustring& label);
  void set_css_class(const ustring& ids, const ustring& class_name);
  void define_class(const std::vector<ustring>& ids, const std::vector<ustring>& style);
  void set_tooltip(const ustring& ids, const std::optional<ustring>& tooltip);
  void set_link(const ustring& ids, const ustring& link, const std::optional<ustring>& target = std::nullopt);
  void set_click_event(const ustring& ids);
  void set_direction(const ustring& dir) { model.direction = dir; }
  void set_acc_title(const ustring& text);
  void set_acc_description(const ustring& text);
  ustring add_namespace(const ustring& id, const std::optional<ustring>& label = std::nullopt);
  void pop_namespace() {
    if (!namespace_stack_.empty()) namespace_stack_.pop_back();
  }
  void add_classes_to_namespace(const ustring& id, const std::vector<ustring>& class_names, const std::vector<ustring>& note_names);
  void set_css_style(const ustring& id, const std::vector<ustring>& styles);

 private:
  void tag(int node, const ustring& css_class);
  int ensure(const ustring& id);
  int find_class(const ustring& name) const;

  std::vector<ustring> namespace_stack_;
  // The classes carrying each css class, in the order they were tagged.
  std::map<ustring, std::vector<int>> with_css_class_;
};

}  // namespace pele::classdiagram
