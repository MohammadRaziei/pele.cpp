#pragma once

#include <string>

#include "class/graph.h"

namespace pele::classdiagram {

// How far the line stops short of the node so that it meets the back of the marker.
double class_marker_trim(end_marker type);

// Draws an end marker whose tip is at (x, y), pointing along (dx, dy).
std::string class_marker(end_marker type, double x, double y, double dx, double dy, const std::string& color);

}  // namespace pele::classdiagram
