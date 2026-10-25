#pragma once

#include <functional>
#include <optional>
#include <string>

#include "render_options.h"
#include "rendered.h"

namespace pele {

// Ways of making a diagram fit the width the host has.
struct fit_width {
  // The width a title may take before it wraps.
  static double title_room(const render_options& options);

  // For a diagram that can run across or down: draws it across, and down instead when the host
  // allows it, the width it has is under the breakpoint, across does not fit, and down is narrower.
  static rendered turn_to_fit(const render_options& options, const std::function<rendered(bool down)>& draw);

  static bool runs_across(const std::optional<std::string>& direction);

  // Draws a diagram with room to spare, and tightly instead when that does not fit the width the
  // host has and tight is narrower.
  // Draws a chart whose plot can be any width so that the whole diagram fits the width the host
  // has. What surrounds the plot (labels, a legend) is only known once it is drawn, so the plot
  // gives up the overflow and is drawn again. `narrow` tells the chart that it did not fit at
  // its own width, for a chart that then arranges itself differently.
  static rendered fit_plot(const render_options& options, double natural, double least, const std::function<rendered(double plot, bool narrow)>& draw);
  // For a chart that is drawn to a set width: its own, or the host's when that is smaller.
  // `around` is what the chart adds outside that width.
  static double cap_width(const render_options& options, double natural, double least, double around = 0);
  static rendered tighten(const render_options& options, const std::function<rendered(bool tight)>& draw);
};

}  // namespace pele
