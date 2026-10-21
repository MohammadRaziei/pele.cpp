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

source_location locate(ustring_view src, std::size_t offset) {
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
