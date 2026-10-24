#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "regex.h"
#include "ustring.h"

namespace pele {

// Tokenizer for the diagram types Mermaid defines with Langium grammars. Token types are tried
// in order and the first match wins, as in Chevrotain.

// Answers as a token's pattern does: the end of the match at `at`, or -1.
using scanner_fn = std::function<std::ptrdiff_t(ustring_view src, std::size_t at)>;

struct token_type;
using token_ptr = std::shared_ptr<const token_type>;

struct token_type {
  std::string name;
  // The pattern is a literal string, or when `pattern` is set, a regular expression read as sticky.
  ustring literal;
  std::shared_ptr<const js_regex> pattern;
  bool hidden = false;
  // Start of a construct that needs a closer. When this matches but the whole pattern does not,
  // no closer exists in the rest of the text, so the pattern is not tried again.
  std::shared_ptr<const js_regex> opener;
  // Every ASCII character a match of a regular expression can start with, when that is known.
  // The type is then tried only at those characters and at characters outside ASCII.
  std::optional<std::string> first;
  // Chevrotain's LONGER_ALT. When this type matches, these are tried at the same place, and the
  // first one that matches more text becomes the token.
  std::vector<token_ptr> longer;
  // Makes a matcher that answers as `pattern` does, for a pattern that could rescan the text.
  // It is called once per text, so the matcher may remember what it has scanned.
  std::function<scanner_fn()> scanner;
  // Matches in place of the pattern, for a pattern that would rescan text from every position
  // and needs no memory between calls.
  scanner_fn match;
};

// Builds a token type: `token_def::regex("NEWLINE", "\\r?\\n", "\r\n").hidden()`.
class token_def {
 public:
  static token_def literal(std::string name, ustring text);
  // `source` is a JavaScript regular expression, matched sticky.
  static token_def regex(std::string name, std::string_view source, std::optional<std::string> first = std::nullopt);

  token_def& hidden();
  token_def& opener(std::string_view source);
  token_def& longer(std::vector<token_ptr> alts);
  token_def& scanner(std::function<scanner_fn()> make);
  token_def& match(scanner_fn fn);

  operator token_ptr() const { return std::make_shared<const token_type>(type_); }

 private:
  token_type type_;
};

// A table of token types in matching order, with what the tokenizer looks up at each character.
class token_set {
 public:
  explicit token_set(std::vector<token_ptr> types);

  std::size_t size() const { return types_.size(); }
  const token_type& operator[](std::size_t kind) const { return *types_[kind]; }
  const std::string& name(int kind) const { return types_[static_cast<std::size_t>(kind)]->name; }

  // The kinds that can start with an ASCII character, in matching order; every kind otherwise.
  const std::vector<int>& starting(unsigned ascii) const { return starting_[ascii]; }
  const std::vector<int>& every() const { return every_; }
  // The kinds tried in place of `kind` when it matches (empty when none).
  const std::vector<int>& longer(int kind) const { return longer_[static_cast<std::size_t>(kind)]; }

 private:
  std::vector<token_ptr> types_;
  std::vector<std::vector<int>> starting_;
  std::vector<int> every_;
  std::vector<std::vector<int>> longer_;
};

struct tokens {
  // The kind of each token, then -1 for the end of the text.
  std::vector<int> kinds;
  std::vector<std::size_t> starts;
  std::vector<std::size_t> ends;
  ustring src;
};

// Splits `src` into tokens. A character no token accepts is a syntax error in `diagram`'s name.
tokens tokenize(ustring_view src, const token_set& types, const std::string& diagram);

namespace common_tokens {

token_ptr acc_descr();
token_ptr acc_title();
token_ptr title();
token_ptr string();
token_ptr newline();
token_ptr whitespace();
token_ptr yaml();
token_ptr directive();
token_ptr single_line_comment();

// Mermaid ends a keyword only at whitespace, a comment, or the end of input.
token_ptr keyword(const std::string& word);

// What an `accTitle`, `accDescr` or `title` token says.
ustring title_value(ustring_view text);
ustring acc_title_value(ustring_view text);
ustring acc_descr_value(ustring_view text);
// The text of a quoted string token, with its escapes read.
ustring string_value(ustring_view text);

}  // namespace common_tokens

// Reads tokens for a hand-written parser, with Chevrotain-style messages on failure.
class reader {
 public:
  reader(tokens t, const token_set& types, std::string diagram) : tokens_(std::move(t)), types_(types), diagram_(std::move(diagram)) {}

  // The kind of the current token, or -1 at the end.
  int kind() const { return i < tokens_.kinds.size() ? tokens_.kinds[i] : -1; }
  ustring text(std::size_t index) const;
  ustring text() const { return text(i); }
  ustring take() { return text(i++); }
  bool accept(int k) {
    if (kind() != k) return false;
    i++;
    return true;
  }
  ustring expect(int k);
  [[noreturn]] void fail(const std::string& expected) const;

  std::size_t i = 0;

 private:
  tokens tokens_;
  const token_set& types_;
  std::string diagram_;
};

}  // namespace pele
