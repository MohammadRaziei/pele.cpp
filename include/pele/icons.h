#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace pele {

// The host's icons for the render in progress. A label leaves room for an icon only when the host
// has one by that name, and layout has to know that before anything is drawn.
class icon_set {
 public:
  using resolver = std::function<std::optional<std::string>(const std::string& name)>;

  icon_set() = default;
  explicit icon_set(resolver r) : resolver_(std::move(r)) {}

  // The markup for an icon, or an empty string where the host has none.
  const std::string& markup(const std::string& name) const {
    auto it = found_.find(name);
    if (it != found_.end()) return it->second;
    std::string m;
    if (resolver_) m = resolver_(name).value_or("");
    return found_.emplace(name, std::move(m)).first->second;
  }

 private:
  resolver resolver_;
  mutable std::unordered_map<std::string, std::string> found_;
};

}  // namespace pele
