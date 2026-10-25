#pragma once

#include <string>

#include "ustring.h"

namespace pele::classdiagram {

// A line inside a class box, split the way Mermaid splits it: a visibility mark, a name, and for a
// method its parameters, classifier and return type.
class class_member {
 public:
  enum class kind { method, attribute };

  class_member(ustring_view input, kind member_type);

  ustring id;
  kind member_type;
  // One of # + ~ -, or empty.
  ustring visibility;
  // $ for static, * for abstract, or empty.
  ustring classifier;
  ustring parameters;
  ustring return_type;

  struct display {
    ustring text;
    std::string css_style;
  };
  display display_details() const;
  std::string parse_classifier() const;

 private:
  bool parse_method(ustring_view input);
  void parse_attribute(ustring_view input);
};

}  // namespace pele::classdiagram
