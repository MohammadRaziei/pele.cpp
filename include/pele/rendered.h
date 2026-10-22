#pragma once

#include <string>
#include <vector>

#include "ustring.h"

namespace pele {

// A link a diagram contains, for the host to act on: a node that links out, or a note link.
struct rendered_link {
  ustring id;
  ustring href;
  bool internal = false;
};

// What a diagram type's renderer gives back.
struct rendered {
  std::string svg;
  double width = 0;
  double height = 0;
  std::vector<rendered_link> links;
};

}  // namespace pele
