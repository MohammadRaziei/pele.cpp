#pragma once

#include "label.h"
#include "text_measurer.h"
#include "ustring.h"

namespace pele {

// Literal labels, including filenames and protocol fields, must retain every character but the
// spaces a line breaks at. Long words can break between code points; measurement stays bounded
// by one line of text.
label literal_label(ustring_view text, const text_measurer& measurer, double size, double room, int style = 0);

}  // namespace pele
