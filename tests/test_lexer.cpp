// Compares the flowchart tokenizer with Pele's TypeScript (tests/data/lexer_cases.txt, made by tools/gen_lexer_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "flowchart/lexer.h"

using namespace pele;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    if (line.rfind("lex ", 0) != 0) continue;
    std::istringstream head(line.substr(4));
    std::string src;
    std::size_t count;
    head >> src >> count;
    struct tok { int type; std::size_t start; ustring text; };
    std::vector<tok> want(count);
    for (std::size_t i = 0; i < count; i++) {
      std::getline(in, line);
      std::istringstream s(line);
      std::string text;
      s >> want[i].type >> want[i].start >> text;
      want[i].text = unhex16(text);
    }
    total++;
    const ustring source = unhex16(src);
    const flowchart::token_list got = flowchart::tokenize(source);
    std::string why;
    if (got.types.size() != count) why = "token count " + std::to_string(got.types.size()) + " vs " + std::to_string(count);
    for (std::size_t i = 0; why.empty() && i < count; i++) {
      if (got.types[i] != want[i].type || got.starts[i] != want[i].start || got.texts[i] != want[i].text)
        why = "token " + std::to_string(i) + " type " + std::to_string(got.types[i]) + "/" + std::to_string(want[i].type) + " start " + std::to_string(got.starts[i]) + "/" +
              std::to_string(want[i].start);
    }
    if (!why.empty()) {
      failed++;
      if (shown++ < 5) std::cout << "differs: " << why << "\n  source: " << to_utf8(source).substr(0, 200) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
