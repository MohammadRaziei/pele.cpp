#include "fit_width.h"

#include <algorithm>

namespace pele {

double fit_width::title_room(const render_options& options) {
  const auto& available = options.max_width;
  return available && *available > 0 ? std::max(80.0, *available - 2 * (options.padding ? *options.padding : 8.0)) : 4000;
}

rendered fit_width::turn_to_fit(const render_options& options, const std::function<rendered(bool)>& draw) {
  rendered across = draw(false);
  const auto& available = options.max_width;
  if (!options.auto_direction || !available || !(*available > 0) || across.width <= *available) return across;
  const auto& breakpoint = options.direction_breakpoint;
  if (*available >= (breakpoint && *breakpoint > 0 ? *breakpoint : 640.0)) return across;
  rendered down = draw(true);
  return down.width < across.width ? down : across;
}

bool fit_width::runs_across(const std::optional<std::string>& direction) { return direction && (*direction == "LR" || *direction == "RL"); }

rendered fit_width::tighten(const render_options& options, const std::function<rendered(bool)>& draw) {
  rendered loose = draw(false);
  const auto& available = options.max_width;
  if (!available || !(*available > 0) || loose.width <= *available) return loose;
  rendered tight = draw(true);
  return tight.width < loose.width ? tight : loose;
}

}  // namespace pele
