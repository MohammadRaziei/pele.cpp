#pragma once

#include <cstdint>

#include "ustring.h"

namespace pele {

// Font styles of a run of text, combined with |.
struct text_style {
  static constexpr int bold = 1;
  static constexpr int italic = 2;
  static constexpr int mono = 4;
  static constexpr int sub = 8;
  static constexpr int sup = 16;
  static constexpr int underline = 32;
};

// How wide text is drawn. Strategy: layout depends only on this interface, so a host with real
// font metrics can supply its own.
class text_measurer {
 public:
  virtual ~text_measurer() = default;
  virtual double width(ustring_view text, double size, int style) const = 0;
};

// Estimates widths from a table of Arial advances; the same answer on every machine.
class metrics_measurer final : public text_measurer {
 public:
  double width(ustring_view text, double size, int style) const override;
};

}  // namespace pele
