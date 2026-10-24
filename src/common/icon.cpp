#include "common/icon.h"

#include "svg.h"

namespace pele {

std::string icon_svg(ustring_view name, double x, double y, double side, const icon_set& icons) {
  return "<svg class=\"pele-icon\" data-icon=\"" + svg::esc_text(name) + "\" x=\"" + svg::num(x) + "\" y=\"" + svg::num(y) + "\" width=\"" + svg::num(side) + "\" height=\"" +
         svg::num(side) + "\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" +
         icons.markup(to_utf8(name)) + "</svg>";
}

}  // namespace pele
