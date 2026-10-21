#pragma once

#include <optional>

#include "ustring.h"
#include "yaml.h"

namespace pele {

// The configuration a diagram carries in its front matter and `%%{init: ...}%%` directives.
using config = yaml_value;

struct preprocessed {
  ustring text;
  std::optional<ustring> title;
  config settings = yaml_value::object();
  // The text before its comment lines are removed, and the number of front matter lines before it.
  ustring with_comments;
  int line_offset = 0;
};

// Reads the front matter and directives off the source and removes comments, as Mermaid does
// before it looks for the diagram type.
preprocessed preprocess(ustring_view source);

// Removes comment lines (`%% ...`) and leading whitespace.
ustring cleanup_comments(const ustring& text);

// Mermaid hides `#name;` entity codes behind placeholder characters so `#` and `;` survive parsing.
ustring encode_entities(const ustring& text);

}  // namespace pele
