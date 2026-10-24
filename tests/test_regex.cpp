// Compares js_regex with JavaScript's RegExp on every regex literal in Pele's sources
// (tests/data/regexes.txt, tests/data/regex_cases.txt; made by tools/gen_regex_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "regex.h"

using namespace pele;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

std::string unhex8(const std::string& s) {
  std::string out;
  for (std::size_t i = 1; i + 2 <= s.size(); i += 2) out += static_cast<char>(std::stoul(s.substr(i, 2), nullptr, 16));
  return out;
}

std::string hex16(ustring_view s) {
  static const char* digits = "0123456789abcdef";
  std::string out = "x";
  for (char16_t c : s) {
    out += digits[(c >> 12) & 15];
    out += digits[(c >> 8) & 15];
    out += digits[(c >> 4) & 15];
    out += digits[c & 15];
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::vector<std::pair<std::string, std::string>> sources;
  {
    std::ifstream in(argv[1]);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream s(line);
      std::string f, p;
      s >> f >> p;
      sources.emplace_back(unhex8(f), unhex8(p));
    }
  }
  std::vector<std::unique_ptr<js_regex>> regexes;
  int compile_failures = 0;
  for (const auto& [flags, source] : sources) {
    try {
      regexes.push_back(std::make_unique<js_regex>(source, flags));
    } catch (const std::exception& e) {
      regexes.push_back(nullptr);
      compile_failures++;
      if (compile_failures <= 5) std::cout << "cannot compile /" << source << "/" << flags << ": " << e.what() << "\n";
    }
  }
  std::ifstream in(argv[2]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string kind, mode, input, want;
    std::size_t idx, at;
    s >> kind >> idx >> mode >> input >> at >> want;
    total++;
    std::string got = "-";
    if (regexes[idx]) {
      const ustring text = unhex16(input);
      try {
        const auto m = mode == "y" ? regexes[idx]->match_at(text, at) : regexes[idx]->search(text, at);
        if (m) {
          got = std::to_string(m->index) + ":" + std::to_string(m->end) + ":";
          for (std::size_t k = 0; k < m->groups.size(); k++) {
            if (k) got += ",";
            if (!m->groups[k]) got += "u";
            else if (k == 0) got += std::to_string(m->index) + "-" + std::to_string(m->end);
            else got += "g" + std::to_string(k) + "=" + hex16(text.substr(m->groups[k]->first, m->groups[k]->second - m->groups[k]->first));
          }
        }
      } catch (const std::exception&) {
        got = "ERR";
      }
    } else {
      got = "NOCOMPILE";
    }
    if (got != want) {
      failed++;
      if (shown++ < 6) std::cout << "regex " << idx << " /" << sources[idx].second << "/" << sources[idx].first << " mode " << mode << " at " << at << "\n  got:  " << got.substr(0, 120) << "\n  want: " << want.substr(0, 120) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " matches agree (" << compile_failures << " regexes did not compile)\n";
  return failed == 0 ? 0 : 1;
}
