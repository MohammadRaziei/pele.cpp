// Compares the class diagram lexer with Pele's TypeScript (tests/data/class_lexer_cases.txt, made by
// tools/gen_class_lexer_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "class/lexer.h"

using namespace pele;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

std::string hex16(const ustring& s) {
  static const char* digits = "0123456789abcdef";
  std::string out = "x";
  for (char16_t c : s)
    for (int shift = 12; shift >= 0; shift -= 4) out += digits[(c >> shift) & 15];
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string tag, src, rest;
    std::size_t count;
    s >> tag >> src >> count;
    std::getline(s, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    const ustring text = unhex16(src);
    const classdiagram::tokens tk = classdiagram::tokenize(text);
    std::string got;
    for (std::size_t i = 0; i < tk.types.size(); i++) got += (i ? "," : "") + std::to_string(tk.types[i]) + ":" + hex16(tk.texts[i]) + ":" + std::to_string(tk.starts[i]);
    total++;
    if (tk.types.size() != count || got != rest) {
      failed++;
      if (shown++ < 4) {
        std::size_t at = 0;
        while (at < got.size() && at < rest.size() && got[at] == rest[at]) at++;
        std::cout << "differs: tokens " << tk.types.size() << " vs " << count << "\n  got:  " << got.substr(at > 40 ? at - 40 : 0, 120) << "\n  want: " << rest.substr(at > 40 ? at - 40 : 0, 120) << "\n  source: " << to_utf8(text).substr(0, 100) << "\n";
      }
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}
