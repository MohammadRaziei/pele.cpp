#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ustring.h"

namespace pele {

// A regular expression with the meaning JavaScript's RegExp gives it (without the `u` flag):
// backtracking, leftmost, greedy or lazy as written. Flags `i`, `m` and `s` are read; `g` and
// `y` are the caller's business, as `match_at` and `search` say which it wants.
//
// Supported: literals and escapes, `.`, classes, groups (capturing or not), alternation,
// `* + ? {n,m}` with lazy forms, `^ $ \b \B`, and lookahead `(?= )` `(?! )`.
class js_regex {
 public:
  // The text of a match and of the groups in it; group 0 is the whole match.
  struct match {
    std::size_t index = 0;
    std::size_t end = 0;
    std::vector<std::optional<std::pair<std::size_t, std::size_t>>> groups;

    ustring group(ustring_view s, std::size_t k) const { return k < groups.size() && groups[k] ? ustring(s.substr(groups[k]->first, groups[k]->second - groups[k]->first)) : ustring(); }
    bool has(std::size_t k) const { return k < groups.size() && groups[k].has_value(); }
  };

  // `pattern` is the source of a JavaScript regular expression, as ASCII or UTF-8.
  explicit js_regex(std::string_view pattern, std::string_view flags = "");
  ~js_regex();
  js_regex(js_regex&&) noexcept;
  js_regex& operator=(js_regex&&) noexcept;

  // A match that starts exactly at `at` (what the sticky flag does).
  std::optional<match> match_at(ustring_view s, std::size_t at) const;
  // The leftmost match that starts at `from` or later.
  std::optional<match> search(ustring_view s, std::size_t from = 0) const;

  bool test_at(ustring_view s, std::size_t at) const { return match_at(s, at).has_value(); }
  bool test(ustring_view s) const { return search(s, 0).has_value(); }

  // Every match, left to right and not overlapping, as the `g` flag finds them.
  std::vector<match> match_all(ustring_view s) const;
  // `String.prototype.replace` with a global pattern, the replacement given by a function.
  ustring replace_all(ustring_view s, const std::function<ustring(const match&)>& fn) const;
  ustring replace_all(ustring_view s, ustring_view replacement) const;
  // The first match only.
  ustring replace_first(ustring_view s, const std::function<ustring(const match&)>& fn) const;

  std::size_t group_count() const;

 // The compiled form. Not for callers.
  struct program;

 private:
  std::unique_ptr<program> program_;
};

}  // namespace pele
