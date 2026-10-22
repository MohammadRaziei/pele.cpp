#pragma once

#include <optional>
#include <string>

namespace pele::flowchart {

// Whether Mermaid accepts the name for a shape, documented or not.
bool is_valid_shape(const std::string& name);

// The documented name of a shape: `proc` is `rect`. An unknown name, or none, is `rect`.
std::string canonical_shape(const std::optional<std::string>& name);

}  // namespace pele::flowchart
