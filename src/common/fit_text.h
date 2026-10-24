#pragma once

#include <optional>

#include "text_measurer.h"
#include "ustring.h"

namespace pele {

struct fitted {
  ustring text;
  double size = 0;
  double width = 0;
};

// Fits one line of text into a width: first by shrinking the font down to `min`, then by cutting
// the text and adding an ellipsis. Gives nothing when not even one character fits.
std::optional<fitted> fit_text(ustring_view text, double max, double size, double min, const text_measurer& measurer, int style = 0);

}  // namespace pele
