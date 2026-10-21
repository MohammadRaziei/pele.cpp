#pragma once

#include <string>

#include "label.h"

namespace pele {

struct titled {
  double width = 0;
  double height = 0;
  std::string content;
};

// Puts a diagram title above content that was laid out without one.
titled with_title(ustring_view title, const label_layouter& layouter, double size, double pad, double width, double height, const std::string& content);

}  // namespace pele
