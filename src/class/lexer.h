#pragma once

#include <vector>

#include "ustring.h"

namespace pele::classdiagram {

struct tokens {
  std::vector<int> types;
  std::vector<ustring> texts;
  std::vector<std::size_t> starts;
};

tokens tokenize(ustring_view src);

}  // namespace pele::classdiagram
