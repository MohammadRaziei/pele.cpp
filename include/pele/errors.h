#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

  pele_error(const std::string& message, error_code code = error_code::syntax) : std::runtime_error(message), message_(message), code_(code) {}
  pele_error(const std::string& message, error_code code, error_info details)
      : std::runtime_error(message), message_(message), code_(code), info_(std::move(details)) {}

  const char* what() const noexcept override { return message_.c_str(); }

  error_code code() const { return code_; }
  const std::optional<std::string>& type() const { return info_.type; }
  const std::optional<std::string>& mermaid_type() const { return info_.mermaid_type; }
  int line() const { return info_.line; }
  int column() const { return info_.column; }
  const std::string& snippet() const { return info_.snippet; }

  // Replaces the message and the snippet, as when entity codes are shown the way they were written.
  void rewrite(std::string message, std::string snippet) {
    message_ = std::move(message);
    info_.snippet = std::move(snippet);
  }

  // Names the diagram type the error belongs to, unless it already says.
  void tag(const std::string& type, const std::string& mermaid_type) {
    if (!info_.type) info_.type = type;
    if (!info_.mermaid_type) info_.mermaid_type = mermaid_type;
  }

 private:
  std::string message_;
  error_code code_;
  error_info info_;
};

// Input range [from, to) replaced by `length` output characters.
struct edit {
  long long from = 0;
  long long to = 0;
  long long length = 0;
};

// The edits of each step that turned the source into the text a diagram parses, in order.
using edit_steps = std::vector<std::vector<edit>>;

// Where an offset of the text after the steps was in the text before them.
long long input_offset(const edit_steps& steps, long long at);

// While alive, positions in `parsed` are reported in the original source: `find` gives the source
// and the offset in it. Scopes nest.
class locating_scope {
 public:
  using finder = std::function<std::pair<ustring, long long>(long long)>;
  locating_scope(const ustring& parsed, finder find);
  ~locating_scope();
  locating_scope(const locating_scope&) = delete;
  locating_scope& operator=(const locating_scope&) = delete;

 private:
  const ustring* outer_parsed_;
  const finder* outer_find_;
  finder find_;
};

// Maps only when `src` is exactly the text being parsed.
std::pair<ustring, long long> written(ustring_view src, long long offset);

// The line and column of an offset in the source, and the line with a caret under the column.
source_location locate(ustring_view src, std::size_t offset);

// A parse error in Mermaid's wording. `lexical` is for text no rule of the grammar accepts.
pele_error syntax_error(const std::string& type, ustring_view src, std::size_t offset, ustring_view detail, bool lexical = false);

}  // namespace pele
