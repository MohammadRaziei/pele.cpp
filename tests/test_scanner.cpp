// Compares pie's number scanner with Pele's TypeScript one (tests/data/scanner_cases.txt, made by
// tools/gen_scanner_cases.mjs), and with the pattern it stands for.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "pie/pie.h"
#include "regex.h"

using namespace pele;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

std::vector<long long> numbers(const std::string& list) {
  std::vector<long long> out;
  if (list == "-") return out;
  std::istringstream s(list);
  std::string n;
  while (std::getline(s, n, ',')) out.push_back(std::stoll(n));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  const js_regex pattern(R"((?:-?[0-9]+\.[0-9]+(?!\.))|(?:-?(0|[1-9][0-9]*)(?!\.)))");
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string tag, src, count, at, want;
    s >> tag >> src >> count >> at >> want;
    const ustring text = unhex16(src);
    const std::vector<long long> positions = numbers(at);
    const std::vector<long long> answers = numbers(want);
    scanner_fn scan = pie::number_scanner();
    std::string why;
    for (std::size_t i = 0; why.empty() && i < positions.size(); i++) {
      const std::size_t p = static_cast<std::size_t>(positions[i]);
      const long long got = scan(text, p);
      const auto m = pattern.match_at(text, p);
      const long long by_pattern = m ? static_cast<long long>(m->end) : -1;
      if (got != answers[i]) why = "at " + std::to_string(p) + ": scanner " + std::to_string(got) + ", TypeScript " + std::to_string(answers[i]);
      else if (by_pattern != got) why = "at " + std::to_string(p) + ": scanner " + std::to_string(got) + ", pattern " + std::to_string(by_pattern);
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 6) std::cout << "differs: " << why << "\n  text: " << to_utf8(text).substr(0, 80) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " texts match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}
