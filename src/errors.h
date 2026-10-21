#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include "ustring.h"

namespace pele {

enum class error_code { unsupported_diagram, syntax, semantic, limit };

// The code as Pele names it: "unsupported-diagram", "syntax", "semantic" or "limit".
const char* to_string(error_code code);

// Where in the source an error is, and a snippet that points at it.
struct source_location {
  int line = 1;
  int column = 1;
  ustring snippet;
};

// What an error says about where it is and what it is about.
struct error_info {
  std::optional<std::string> type;
  std::optional<std::string> mermaid_type;
  int line = 0;
  int column = 0;
  std::string snippet;
};

// Everything Pele refuses to draw, and why.
class pele_error : public std::runtime_error {
 public:
  using info = error_info;

  pele_error(const std::string& message, error_code code = error_code::syntax) : std::runtime_error(message), code_(code) {}
  pele_error(const std::string& message, error_code code, error_info details) : std::runtime_error(message), code_(code), info_(std::move(details)) {}

  error_code code() const { return code_; }
  const std::optional<std::string>& type() const { return info_.type; }
  const std::optional<std::string>& mermaid_type() const { return info_.mermaid_type; }
  int line() const { return info_.line; }
  int column() const { return info_.column; }
  const std::string& snippet() const { return info_.snippet; }

 private:
  error_code code_;
  error_info info_;
};

// The line and column of an offset in the source, and the line with a caret under the column.
source_location locate(ustring_view src, std::size_t offset);

// A parse error in Mermaid's wording. `lexical` is for text no rule of the grammar accepts.
pele_error syntax_error(const std::string& type, ustring_view src, std::size_t offset, ustring_view detail, bool lexical = false);

}  // namespace pele
