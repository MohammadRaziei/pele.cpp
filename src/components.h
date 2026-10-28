#pragma once

#include <optional>
#include <vector>

#include "compound.h"

namespace pele {

// Moves disconnected parts into rows without changing the layout inside a part. Gives the size of
// the result, or nothing when the parts do not fit better in rows.
std::optional<compound_result> wrap_components(std::vector<compound_node>& nodes, std::vector<compound_edge>& edges, double width, double gap);

}  // namespace pele
