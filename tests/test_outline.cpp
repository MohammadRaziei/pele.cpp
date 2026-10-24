// Compares the outline tokenizer (src/common/outline) and changes_when_lowercased with Pele's
// TypeScript (tests/data/outline_cases.txt, made by tools/gen_outline_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "common/outline.h"
#include "lowercase.h"

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
    std::string tag, src;
    s >> tag;
    std::string why;
    ustring text;
    if (tag == "t") {
      int kanban;
      std::size_t count;
      std::string rest;
      s >> kanban >> src >> count;
      std::getline(s, rest);
      if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
      text = unhex16(src);
      const outline::tokens tk = outline::tokenize(text, kanban != 0);
      std::string got;
      for (std::size_t i = 0; i < tk.types.size(); i++) got += (i ? "," : "") + std::to_string(tk.types[i]) + ":" + hex16(tk.texts[i]) + ":" + std::to_string(tk.starts[i]);
      if (tk.types.size() != count || got != rest) why = std::string("tokens (") + (kanban ? "kanban" : "mindmap") + ")\n  got:  " + got.substr(0, 160) + "\n  want: " + rest.substr(0, 160);
    } else if (tag == "l") {
      int want;
      s >> src >> want;
      text = unhex16(src);
      if (changes_when_lowercased(text) != (want != 0)) why = std::string("lowercase: want ") + std::to_string(want);
    } else {
      continue;
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 5) std::cout << "differs: " << why << "\n  source: " << to_utf8(text).substr(0, 100) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}
