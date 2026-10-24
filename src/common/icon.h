#pragma once

#include <string>

#include "icons.h"
#include "ustring.h"

namespace pele {

// A square for a named icon, in the form labels use. The host's icons supply the drawing, if any.
std::string icon_svg(ustring_view name, double x, double y, double side, const icon_set& icons);

}  // namespace pele
