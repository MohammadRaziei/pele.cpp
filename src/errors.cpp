#include "errors.h"

namespace pele {

const char* to_string(error_code code) {
  switch (code) {
    case error_code::unsupported_diagram: return "unsupported-diagram";
    case error_code::syntax: return "syntax";
    case error_code::semantic: return "semantic";
    default: return "limit";
  }
}

long long input_offset(const edit_steps& steps, long long at) {
  for (std::size_t s = steps.size(); s-- > 0;) {
    long long shift = 0;
    bool found = false;
    for (const edit& e : steps[s]) {
      const long long start = e.from - shift;
      if (at < start) break;
      if (at < start + e.length) {
        const long long inside = at - start;
        const long long last = e.to - e.from - 1;
        at = e.from + (inside < last ? inside : last);
        found = true;
        break;
      }
      shift += e.to - e.from - e.length;
    }
    if (!found) at += shift;
  }
  return at;
}

namespace {
thread_local const ustring* g_parsed = nullptr;
thread_local const locating_scope::finder* g_find = nullptr;
}  // namespace

locating_scope::locating_scope(const ustring& parsed, finder find) : outer_parsed_(g_parsed), outer_find_(g_find), find_(std::move(find)) {
  g_parsed = &parsed;
  g_find = &find_;
}

locating_scope::~locating_scope() {
  g_parsed = outer_parsed_;
  g_find = outer_find_;
}

std::pair<ustring, long long> written(ustring_view src, long long offset) {
  if (g_parsed != nullptr && src == ustring_view(*g_parsed)) return (*g_find)(offset);
  return {ustring(src), offset};
}

source_location locate(ustring_view original, std::size_t original_offset) {
  const auto mapped = written(original, static_cast<long long>(original_offset));
  const ustring& src = mapped.first;
  const std::size_t offset = static_cast<std::size_t>(mapped.second);
  int line = 1;
  std::size_t line_start = 0;
  for (std::size_t i = src.find(u'\n'); i != ustring_view::npos && i < offset; i = src.find(u'\n', i + 1)) {
    line++;
    line_start = i + 1;
  }
  std::size_t line_end = src.find(u'\n', offset);
  if (line_end == ustring_view::npos) line_end = src.size();
  const int column = static_cast<int>(offset - line_start + 1);
  source_location loc;
  loc.line = line;
  loc.column = column;
  loc.snippet = ustring(src.substr(line_start, line_end - line_start)) + u"\n" + ustring(static_cast<std::size_t>(column - 1), u'-') + u"^";
  return loc;
}

pele_error syntax_error(const std::string& type, ustring_view src, std::size_t offset, ustring_view detail, bool lexical) {
  const source_location loc = locate(src, offset);
  const std::string snippet = to_utf8(loc.snippet);
  const std::string head = lexical ? "Lexical error on line " + std::to_string(loc.line) + ". Unrecognized text." : "Parse error on line " + std::to_string(loc.line) + ":";
  const std::string message = lexical ? head + "\n" + snippet : head + "\n" + snippet + "\n" + to_utf8(detail);
  pele_error::info info;
  info.type = type;
  info.line = loc.line;
  info.column = loc.column;
  info.snippet = snippet;
  return pele_error(message, error_code::syntax, std::move(info));
}

}  // namespace pele
