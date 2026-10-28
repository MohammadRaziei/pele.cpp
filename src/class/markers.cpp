#include "class/markers.h"

#include "edges.h"
#include "svg.h"

namespace pele::classdiagram {
namespace {

constexpr double kTriangle = 12;
constexpr double kDiamond = 16;
constexpr double kHalf = 6;
constexpr double kRing = 5;

}  // namespace

double class_marker_trim(end_marker type) {
  switch (type) {
    case end_marker::extension: return kTriangle;
    case end_marker::composition:
    case end_marker::aggregation: return kDiamond;
    case end_marker::lollipop: return 2 * kRing;
    case end_marker::dependency: return edge_geometry::marker_trim("arrow_point");
    case end_marker::none: break;
  }
  return 0;
}

std::string class_marker(end_marker type, double x, double y, double dx, double dy, const std::string& color) {
  using svg::num;
  const double nx = -dy;
  const double ny = dx;
  switch (type) {
    case end_marker::extension: {
      const double bx = x - dx * kTriangle;
      const double by = y - dy * kTriangle;
      return "<path class=\"pele-marker\" d=\"M" + num(x) + "," + num(y) + "L" + num(bx + nx * kHalf) + "," + num(by + ny * kHalf) + "L" + num(bx - nx * kHalf) + "," +
             num(by - ny * kHalf) + "Z\" stroke-linejoin=\"round\"/>";
    }
    case end_marker::composition:
    case end_marker::aggregation: {
      const double mx = x - (dx * kDiamond) / 2;
      const double my = y - (dy * kDiamond) / 2;
      const double half = kHalf - 1;
      return "<path class=\"pele-marker\" d=\"M" + num(x) + "," + num(y) + "L" + num(mx + nx * half) + "," + num(my + ny * half) + "L" + num(x - dx * kDiamond) + "," +
             num(y - dy * kDiamond) + "L" + num(mx - nx * half) + "," + num(my - ny * half) + "Z\" stroke-linejoin=\"round\"" +
             (type == end_marker::composition ? " fill=\"" + color + "\"" : std::string()) + "/>";
    }
    case end_marker::lollipop:
      return "<circle class=\"pele-marker\" cx=\"" + num(x - dx * kRing) + "\" cy=\"" + num(y - dy * kRing) + "\" r=\"" + std::string("5") + "\"/>";
    case end_marker::dependency: return edge_geometry::marker("arrow_point", x, y, dx, dy, color);
    case end_marker::none: break;
  }
  return "";
}

}  // namespace pele::classdiagram
