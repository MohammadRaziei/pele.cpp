#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "ustring.h"

namespace pele {

// A value read from YAML or JSON: what JavaScript would hold after js-yaml (JSON schema) or JSON.parse.
class yaml_value {
 public:
  enum class kind { null, boolean, number, string, array, object };

  yaml_value() = default;
  static yaml_value boolean(bool v);
  static yaml_value number(double v);
  static yaml_value string(ustring v);
  static yaml_value array(std::vector<yaml_value> items = {});
  static yaml_value object();

  kind type() const { return kind_; }
  bool is_null() const { return kind_ == kind::null; }
  bool is_object() const { return kind_ == kind::object; }
  bool is_array() const { return kind_ == kind::array; }
  bool is_string() const { return kind_ == kind::string; }
  bool is_number() const { return kind_ == kind::number; }
  bool is_boolean() const { return kind_ == kind::boolean; }

  bool as_boolean() const { return boolean_; }
  double as_number() const { return number_; }
  const ustring& as_string() const { return string_; }
  const std::vector<yaml_value>& items() const { return items_; }
  std::vector<yaml_value>& items() { return items_; }

  // Object members, in the order JavaScript lists the keys of an object: those that are array
  // indices first, in numeric order, then the rest in the order they were added.
  const std::vector<ustring>& keys() const { return keys_; }
  const yaml_value* find(ustring_view key) const;
  yaml_value* find(ustring_view key);
  const yaml_value* find(const char* key) const { return find(ascii(key)); }
  yaml_value* find(const char* key) { return find(ascii(key)); }
  void set(ustring key, yaml_value value);

  // JavaScript's truthiness: null, false, 0, NaN and "" are not.
  bool truthy() const;
  // String(value).
  ustring to_js_string() const;

 private:
  kind kind_ = kind::null;
  bool boolean_ = false;
  double number_ = 0;
  ustring string_;
  std::vector<yaml_value> items_;
  std::vector<ustring> keys_;
  std::vector<yaml_value> values_;
};

// What went wrong reading YAML or JSON.
class yaml_error : public std::runtime_error {
 public:
  explicit yaml_error(const std::string& message) : std::runtime_error(message) {}
};

// A small YAML reader covering what Mermaid feeds to js-yaml with the JSON schema: block and flow
// collections, quoted and plain scalars, and block scalars.
yaml_value parse_yaml(ustring_view src);

// JSON.parse.
yaml_value parse_json(ustring_view src);

}  // namespace pele
