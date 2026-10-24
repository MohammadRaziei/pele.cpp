#include "common/tokens.h"

#include <algorithm>

#include "errors.h"

namespace pele {

token_def token_def::literal(std::string name, ustring text) {
  token_def d;
  d.type_.name = std::move(name);
  d.type_.literal = std::move(text);
  return d;
}

token_def token_def::regex(std::string name, std::string_view source, std::optional<std::string> first) {
  token_def d;
  d.type_.name = std::move(name);
  d.type_.pattern = std::make_shared<const js_regex>(source);
  d.type_.first = std::move(first);
  return d;
}

token_def& token_def::hidden() {
  type_.hidden = true;
  return *this;
}

token_def& token_def::opener(std::string_view source) {
  type_.opener = std::make_shared<const js_regex>(source);
  return *this;
}

token_def& token_def::longer(std::vector<token_ptr> alts) {
  type_.longer = std::move(alts);
  return *this;
}

token_def& token_def::scanner(std::function<scanner_fn()> make) {
  type_.scanner = std::move(make);
  return *this;
}

token_def& token_def::match(scanner_fn fn) {
  type_.match = std::move(fn);
  return *this;
}

token_set::token_set(std::vector<token_ptr> types) : types_(std::move(types)) {
  for (std::size_t k = 0; k < types_.size(); k++) every_.push_back(static_cast<int>(k));
  starting_.resize(128);
  for (unsigned c = 0; c < 128; c++) {
    for (int k : every_) {
      const token_type& type = *types_[static_cast<std::size_t>(k)];
      bool starts;
      if (type.pattern == nullptr) starts = !type.literal.empty() && type.literal[0] == c;
      else starts = !type.first || type.first->find(static_cast<char>(c)) != std::string::npos;
      if (starts) starting_[c].push_back(k);
    }
  }
  for (const token_ptr& type : types_) {
    std::vector<int> alts;
    for (const token_ptr& alt : type->longer) {
      // `types.indexOf(alt)`: a type is known by identity, and one that is not in the table is -1.
      const auto at = std::find(types_.begin(), types_.end(), alt);
      alts.push_back(at == types_.end() ? -1 : static_cast<int>(at - types_.begin()));
    }
    longer_.push_back(std::move(alts));
  }
}

tokens tokenize(ustring_view src, const token_set& types, const std::string& diagram) {
  tokens out;
  out.src = ustring(src);
  std::vector<char> dead(types.size(), 0);
  std::vector<scanner_fn> scanners(types.size());
  const std::size_t n = src.size();

  const auto match = [&](int k, std::size_t p) -> std::ptrdiff_t {
    if (k < 0) return -1;
    const std::size_t ki = static_cast<std::size_t>(k);
    if (dead[ki]) return -1;
    const token_type& type = types[ki];
    if (type.match) return type.match(src, p);
    if (type.scanner) {
      if (!scanners[ki]) scanners[ki] = type.scanner();
      return scanners[ki](src, p);
    }
    if (type.pattern == nullptr) {
      if (src.compare(p, type.literal.size(), type.literal) == 0 && p + type.literal.size() <= n) return static_cast<std::ptrdiff_t>(p + type.literal.size());
      return -1;
    }
    if (const auto m = type.pattern->match_at(src, p)) return static_cast<std::ptrdiff_t>(m->end);
    if (type.opener != nullptr && type.opener->test_at(src, p)) dead[ki] = 1;
    return -1;
  };

  std::size_t p = 0;
  while (p < n) {
    const char16_t c = src[p];
    const std::vector<int>& candidates = c < 128 ? types.starting(c) : types.every();
    bool found = false;
    for (int k : candidates) {
      std::ptrdiff_t end = match(k, p);
      if (end <= static_cast<std::ptrdiff_t>(p)) continue;
      int kind = k;
      for (int alt : types.longer(k)) {
        const std::ptrdiff_t alt_end = match(alt, p);
        if (alt_end > end) {
          kind = alt;
          end = alt_end;
          break;
        }
      }
      if (!types[static_cast<std::size_t>(kind)].hidden) {
        out.kinds.push_back(kind);
        out.starts.push_back(p);
        out.ends.push_back(static_cast<std::size_t>(end));
      }
      p = static_cast<std::size_t>(end);
      found = true;
      break;
    }
    if (!found) throw syntax_error(diagram, src, p, u"", true);
  }
  out.kinds.push_back(-1);
  out.starts.push_back(n);
  out.ends.push_back(n);
  return out;
}

namespace common_tokens {
namespace {

// Each shared type is made once, so that a table lists the very object `longer` names.
token_ptr once(token_def def) { return def; }

const js_regex& re_acc_descr() {
  static const js_regex re(R"(accDescr(?:[\t ]*:([^\n\r]*)|\s*{([^}]*)}))");
  return re;
}
const js_regex& re_acc_title() {
  static const js_regex re(R"(accTitle[\t ]*:([^\n\r]*))");
  return re;
}
const js_regex& re_title() {
  static const js_regex re(R"(title([\t ][^\n\r]*|))");
  return re;
}

ustring single(ustring_view text) {
  static const js_regex two_blanks(R"([\t ]{2,})", "m");
  return two_blanks.replace_all(trim(text), ustring_view(u" "));
}

}  // namespace

token_ptr acc_descr() {
  static const token_ptr t = once(token_def::regex("ACC_DESCR", R"([\t ]*accDescr(?:[\t ]*:([^\n\r]*?(?=%%)|[^\n\r]*)|\s*{([^}]*)}))", "\t a"));
  return t;
}

token_ptr acc_title() {
  static const token_ptr t = once(token_def::regex("ACC_TITLE", R"([\t ]*accTitle[\t ]*:(?:[^\n\r]*?(?=%%)|[^\n\r]*))", "\t a"));
  return t;
}

token_ptr title() {
  static const token_ptr t = once(token_def::regex("TITLE", R"([\t ]*title(?:[\t ][^\n\r]*?(?=%%)|[\t ][^\n\r]*|))", "\t t"));
  return t;
}

token_ptr string() {
  static const token_ptr t = once(token_def::regex("STRING", R"("([^"\\]|\\.)*"|'([^'\\]|\\.)*')", "\"'"));
  return t;
}

token_ptr newline() {
  static const token_ptr t = once(token_def::regex("NEWLINE", R"(\r?\n)", "\r\n"));
  return t;
}

token_ptr whitespace() {
  static const token_ptr t = once(token_def::regex("WHITESPACE", R"([\t ]+)", "\t ").hidden());
  return t;
}

token_ptr yaml() {
  static const token_ptr t = once(token_def::regex("YAML", R"(---[\t ]*\r?\n(?:[\S\s]*?\r?\n)?---(?:\r?\n|(?!\S)))", "-").hidden().opener(R"(---[\t ]*\r?\n)"));
  return t;
}

token_ptr directive() {
  static const token_ptr t = once(token_def::regex("DIRECTIVE", R"([\t ]*%%{[\S\s]*?}%%(?:\r?\n|(?!\S)))", "\t %").hidden().opener(R"([\t ]*%%{)"));
  return t;
}

token_ptr single_line_comment() {
  static const token_ptr t = once(token_def::regex("SINGLE_LINE_COMMENT", R"([\t ]*%%[^\n\r]*)", "\t %").hidden());
  return t;
}

token_ptr keyword(const std::string& word) {
  static const std::string special = ".*+?^${}()|[]\\";
  std::string escaped;
  for (char c : word) {
    if (special.find(c) != std::string::npos) escaped += '\\';
    escaped += c;
  }
  return once(token_def::regex(word, escaped + R"((?:(?=%%)|(?!\S)))", std::string(1, word.empty() ? '\0' : word[0])));
}

ustring title_value(ustring_view text) {
  const auto m = re_title().search(text);
  return single(m ? m->group(text, 1) : ustring());
}

ustring acc_title_value(ustring_view text) {
  const auto m = re_acc_title().search(text);
  return single(m ? m->group(text, 1) : ustring());
}

ustring acc_descr_value(ustring_view text) {
  const auto m = re_acc_descr().search(text);
  if (!m) return ustring();
  if (m->has(1)) return single(m->group(text, 1));
  static const js_regex line_start(R"(^\s*)", "m");
  static const js_regex line_end(R"(\s+$)", "m");
  static const js_regex blanks(R"([\t ]{2,})", "m");
  static const js_regex breaks(R"([\n\r]{2,})", "m");
  ustring body = m->group(text, 2);
  body = line_start.replace_all(body, ustring_view(u""));
  body = line_end.replace_all(body, ustring_view(u""));
  body = blanks.replace_all(body, ustring_view(u" "));
  return breaks.replace_all(body, ustring_view(u"\n"));
}

ustring string_value(ustring_view text) {
  if (text.find(u'\\') == ustring_view::npos) return ustring(text.size() >= 2 ? text.substr(1, text.size() - 2) : ustring_view());
  ustring out;
  for (std::size_t i = 1; i + 1 < text.size(); i++) {
    const char16_t c = text[i];
    if (c == u'\\') {
      const char16_t next = ++i < text.size() ? text[i] : u'\0';
      switch (next) {
        case u'b': out += u'\b'; break;
        case u'f': out += u'\f'; break;
        case u'n': out += u'\n'; break;
        case u'r': out += u'\r'; break;
        case u't': out += u'\t'; break;
        case u'v': out += u'\v'; break;
        case u'0': out += u'\0'; break;
        default: out += next;
      }
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace common_tokens

ustring reader::text(std::size_t index) const {
  if (index >= tokens_.kinds.size()) return ustring();
  return tokens_.src.substr(tokens_.starts[index], tokens_.ends[index] - tokens_.starts[index]);
}

ustring reader::expect(int k) {
  if (kind() != k) fail("token of type '" + types_.name(k) + "'");
  return take();
}

void reader::fail(const std::string& expected) const {
  const std::size_t at = std::min(i, tokens_.kinds.size() - 1);
  const ustring found = kind() == -1 ? ustring(u"end of input") : u"`" + text() + u"`";
  throw syntax_error(diagram_, tokens_.src, tokens_.starts[at], to_utf16("Expecting " + expected + " but found ") + found + u".");
}

}  // namespace pele
