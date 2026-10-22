#pragma once

#include <vector>

#include "tokens.h"
#include "ustring.h"

namespace pele::flowchart {

// The tokens of a flowchart source: for each, its type, its text and where it starts.
struct token_list {
  std::vector<int> types;
  std::vector<ustring> texts;
  std::vector<std::size_t> starts;
};

// Splits flowchart text into tokens, by hand and in one pass, the way Mermaid's jison lexer does:
// the same states, the same longest-match rules.
token_list tokenize(const ustring& source);

}  // namespace pele::flowchart
