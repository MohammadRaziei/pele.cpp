#pragma once

#include "ustring.h"

namespace pele {

// `s !== s.toLowerCase()`: whether any character of `s` has a different lower case form.
bool changes_when_lowercased(ustring_view s);

}  // namespace pele
