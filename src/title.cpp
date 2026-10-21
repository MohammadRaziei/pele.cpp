#include "title.h"

#include <algorithm>
#include <cmath>

#include "svg.h"

namespace pele {

titled with_title(ustring_view title, const label_layouter& layouter, double size, double pad, double width, double height, const std::string& content) {
  const label l = layouter.layout(title, false, size, 4000, text_style::bold);
  if (l.height == 0) return {width, height, content};
  const double shift = l.height + 12;
  const double full = std::max(width, std::ceil(l.width + 2 * pad));
  titled out;
  out.width = full;
  out.height = std::ceil(height + shift);
  out.content = svg::label_svg(l, full / 2, pad + l.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") + "<g transform=\"translate(" +
                svg::num((full - width) / 2) + "," + svg::num(shift) + ")\">" + content + "</g>";
  return out;
}

}  // namespace pele
