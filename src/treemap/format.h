#pragma once

#include <string>

#include "ustring.h"

namespace pele::treemap {

// The parts of d3-format's specifier that treemap values use: an optional `$`, `,` for thousands
// separators, a precision, and a type of `f`, `%`, `d`, `e` or none. Fill, alignment and width are
// read and ignored. Anything else falls back to the default, `,`.
class value_formatter {
 public:
  explicit value_formatter(ustring_view spec);
  std::string operator()(double value) const;

 private:
  bool plus_ = false;
  bool comma_ = false;
  bool trim_ = false;
  bool has_precision_ = false;
  int precision_ = 0;
  std::string type_;
  std::string prefix_;
};

}  // namespace pele::treemap
