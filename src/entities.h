#pragma once

#include "ustring.h"

namespace pele {

// Turns Mermaid's `#35;` / `#quot;` codes (as hidden by encode_entities) and HTML entities into characters.
ustring decode_entities(ustring text);

// Shows the placeholders that hide an entity code the way it was written: `#35;`.
ustring written_codes(ustring_view text);

}  // namespace pele
