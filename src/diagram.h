#pragma once

#include <map>
#include <memory>
#include <optional>

#include "detect.h"
#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele {

// What a diagram type's parser gives its renderer.
class diagram_model {
 public:
  virtual ~diagram_model() = default;
};

struct parse_limits {
  std::optional<double> max_edges;
};

// A type of diagram: how to read its text and how to draw what was read.
class diagram {
 public:
  virtual ~diagram() = default;

  virtual diagram_type type() const = 0;
  // The part of the config that belongs to this type.
  virtual const char* section() const { return to_string(type()); }
  // Whether the parser wants the text with its comment lines.
  virtual bool keep_comments() const { return false; }

  virtual std::unique_ptr<diagram_model> parse(const ustring& source, const config& settings, const std::optional<ustring>& title, int line_offset,
                                               const parse_limits& limits) const = 0;
  virtual rendered render(const diagram_model& model, const config& settings, const render_options& options) const = 0;
};

// The diagram types that can be drawn, by type. Registry.
class diagram_registry {
 public:
  // The registry with every built-in type in it.
  static const diagram_registry& builtin();

  void add(std::unique_ptr<diagram> d);
  const diagram* find(diagram_type type) const;

 private:
  std::map<diagram_type, std::unique_ptr<diagram>> diagrams_;
};

}  // namespace pele
