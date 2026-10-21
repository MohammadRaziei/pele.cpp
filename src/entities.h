#pragma once

#include "ustring.h"

namespace pele {

// Turns Mermaid's `#35;` / `#quot;` codes (as hidden by encode_entities) and HTML entities into characters.
ustring decode_entities(ustring text);

}  // namespace pele
