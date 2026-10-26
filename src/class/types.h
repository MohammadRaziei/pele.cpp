#pragma once

#include <map>
#include <optional>
#include <vector>

#include "class/members.h"
#include "ustring.h"

namespace pele::classdiagram {

// Mermaid's relationType numbers; -1 stands for 'none'.
constexpr int kEndNone = -1;
constexpr int kEndAggregation = 0;
constexpr int kEndExtension = 1;
constexpr int kEndComposition = 2;
constexpr int kEndDependency = 3;
constexpr int kEndLollipop = 4;

struct class_node {
  ustring id;
  ustring type;
  ustring label;
  ustring css_classes;
  std::vector<class_member> methods;
  std::vector<class_member> members;
  std::vector<ustring> annotations;
  std::vector<ustring> styles;
  std::optional<ustring> parent;
  std::optional<ustring> link;
  std::optional<ustring> link_target;
  bool have_callback = false;
  std::optional<ustring> tooltip;
};

struct class_relation {
  ustring id1;
  ustring id2;
  ustring relation_title1;
  ustring relation_title2;
  std::optional<ustring> title;
  int type1 = kEndNone;
  int type2 = kEndNone;
  int line_type = 0;
};

struct class_note {
  ustring id;
  std::optional<ustring> cls;
  ustring text;
  int index = 0;
  std::optional<ustring> parent;
};

struct class_interface {
  ustring id;
  ustring label;
  ustring class_id;
};

// The classes, notes and namespaces of a namespace are indices into the model's lists.
struct namespace_node {
  ustring id;
  ustring label;
  std::vector<int> classes;
  std::vector<int> notes;
  std::vector<int> children;
  std::optional<ustring> parent;
  // False for the ancestors created on the way to a dotted name such as `A.B.C`.
  bool explicit_ = false;
};

struct style_class {
  ustring id;
  std::vector<ustring> styles;
  std::vector<ustring> text_styles;
};

// Lists keep the order of insertion, as the maps of Mermaid's model do; the indices find an entry by name.
struct class_model {
  ustring direction = u"TB";
  std::vector<class_node> classes;
  std::map<ustring, int> class_index;
  std::vector<class_relation> relations;
  std::vector<class_note> notes;
  std::map<ustring, int> note_index;
  std::vector<namespace_node> namespaces;
  std::map<ustring, int> namespace_index;
  std::vector<class_interface> interfaces;
  std::vector<style_class> style_classes;
  std::map<ustring, int> style_class_index;
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
};

}  // namespace pele::classdiagram
