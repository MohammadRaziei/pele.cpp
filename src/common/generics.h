#pragma once

#include "ustring.h"

namespace pele {

// Turns `List~int~` into `List<int>`, with the quirks of Mermaid's parseGenericTypes around commas.
ustring parse_generic_types(ustring_view input);

}  // namespace pele
