// Compares the tokenizer and the value functions of src/common/tokens with Pele's TypeScript
// (tests/data/token_tables.txt and token_cases.txt, made by tools/gen_tokens_cases.mjs).
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "common/tokens.h"
#include "errors.h"
#include "pie/pie.h"

using namespace pele;

namespace {

std::string unhex8(const std::string& s) {
  std::string out;
  for (std::size_t i = 1; i + 2 <= s.size(); i += 2) out += static_cast<char>(std::stoul(s.substr(i, 2), nullptr, 16));
  return out;
}

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

struct spec {
  std::string name, kind, pattern, opener, first;
  bool hidden = false, has_opener = false, has_first = false;
  std::vector<int> longer;
};

struct table {
  std::string name;
  std::vector<spec> specs;
  // Built from the patterns alone, as a scanner has to answer as its pattern does.
  std::unique_ptr<token_set> set;
  // The table as the library has it, with its scanners, when a test has it.
  const token_set* library = nullptr;
};

// Makes the types in the order their `longer` lists need, so that each is the one object the lists name.
std::unique_ptr<token_set> build(const std::vector<spec>& specs) {
  std::vector<token_ptr> made(specs.size());
  std::function<token_ptr(std::size_t)> make = [&](std::size_t k) -> token_ptr {
    if (made[k]) return made[k];
    const spec& s = specs[k];
    token_def def = s.kind == "L" ? token_def::literal(s.name, to_utf16(s.pattern)) : token_def::regex(s.name, s.pattern, s.has_first ? std::optional<std::string>(s.first) : std::nullopt);
    if (s.hidden) def.hidden();
    if (s.has_opener) def.opener(s.opener);
    std::vector<token_ptr> alts;
    for (int a : s.longer) alts.push_back(make(static_cast<std::size_t>(a)));
    if (!alts.empty()) def.longer(alts);
    return made[k] = def;
  };
  std::vector<token_ptr> types;
  for (std::size_t k = 0; k < specs.size(); k++) types.push_back(make(k));
  return std::make_unique<token_set>(types);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::map<int, table> tables;
  {
    std::ifstream in(argv[1]);
    std::string line;
    int current = -1;
    while (std::getline(in, line)) {
      std::istringstream s(line);
      std::string tag;
      s >> tag;
      if (tag == "table") {
        std::string name;
        std::size_t count;
        s >> current >> count >> name;
        tables[current].name = name;
      } else if (tag == "t") {
        spec sp;
        std::string name, pattern, hidden, opener, first, longer;
        s >> name >> sp.kind >> pattern >> hidden >> opener >> first >> longer;
        sp.name = unhex8(name);
        sp.pattern = unhex8(pattern);
        sp.hidden = hidden == "1";
        if (opener != "-") sp.has_opener = true, sp.opener = unhex8(opener);
        if (first != "-") sp.has_first = true, sp.first = unhex8(first);
        if (longer != "-") {
          std::istringstream ls(longer);
          std::string n;
          while (std::getline(ls, n, ',')) sp.longer.push_back(std::stoi(n));
        }
        tables[current].specs.push_back(sp);
      }
    }
    for (auto& entry : tables) {
      entry.second.set = build(entry.second.specs);
      if (entry.second.name == "pie") entry.second.library = &pie::pie_tokens();
    }
  }

  std::ifstream in(argv[2]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string tag, why;
    s >> tag;
    ustring source;
    if (tag == "c") {
      int t;
      std::string src, kind;
      s >> t >> src >> kind;
      std::string rest;
      std::getline(s, rest);
      if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
      source = unhex16(src);
      const table& tb = tables.at(t);
      const auto run = [&](const token_set& set) {
        std::string got;
        try {
          const tokens tk = tokenize(source, set, tb.name);
          got = "ok";
          std::string sep = " ";
          for (std::size_t i = 0; i < tk.kinds.size(); i++, sep = ",") got += sep + std::to_string(tk.kinds[i]) + ":" + std::to_string(tk.starts[i]) + ":" + std::to_string(tk.ends[i]);
        } catch (const pele_error& e) {
          got = "err x";
          static const char* digits = "0123456789abcdef";
          for (unsigned char c : std::string(e.what())) got += std::string(1, digits[c >> 4]) + digits[c & 15];
        }
        return got;
      };
      std::string got = run(*tb.set);
      const std::string library = tb.library != nullptr ? run(*tb.library) : got;
      const std::string want = kind + (rest.empty() ? "" : " " + rest);
      if (got != want) why = tb.name + " (patterns): " + got.substr(0, 120) + "\n  want " + want.substr(0, 120);
      else if (library != want) why = tb.name + " (library scanner): " + library.substr(0, 120) + "\n  want " + want.substr(0, 120);
    } else if (tag == "v") {
      std::string kind, src, ok, hex;
      s >> kind >> src >> ok >> hex;
      source = unhex16(src);
      ustring got;
      if (kind == "title") got = common_tokens::title_value(source);
      else if (kind == "accTitle") got = common_tokens::acc_title_value(source);
      else if (kind == "accDescr") got = common_tokens::acc_descr_value(source);
      else got = common_tokens::string_value(source);
      if (ok == "throw") continue;
      if (got != unhex16(hex)) why = kind + " value: " + to_utf8(got).substr(0, 60) + " vs " + to_utf8(unhex16(hex)).substr(0, 60);
    } else {
      continue;
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 6) std::cout << "differs: " << why << "\n  source: " << to_utf8(source).substr(0, 120) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
