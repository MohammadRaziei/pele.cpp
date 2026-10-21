// Compares theme.h with Pele's TypeScript (tests/data/theme_cases.txt, made by tools/gen_theme_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "theme.h"

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

std::optional<std::string> family(const std::string& tok) { return tok == "-" ? std::nullopt : std::optional<std::string>(to_utf8(unhex16(tok))); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string kind;
    s >> kind;
    std::string got, want;
    if (kind == "font") {
      double size;
      std::string fam, mono, out;
      s >> size >> fam >> mono >> out;
      want = unhex8(out);
      got = theme::font_style(size, family(fam), family(mono));
    } else if (kind == "style") {
      int n;
      s >> n;
      std::vector<ustring> decls;
      for (int i = 0; i < n; i++) {
        std::string t;
        s >> t;
        decls.push_back(unhex16(t));
      }
      std::string shape, ln, text, stroke, size;
      int bold, italic;
      s >> shape >> ln >> text >> stroke >> bold >> italic >> size;
      want = unhex8(shape) + "|" + unhex8(ln) + "|" + unhex8(text) + "|" + (stroke == "-" ? "-" : unhex8(stroke)) + "|" + std::to_string(bold) + std::to_string(italic) + "|" + size;
      const theme::resolved_style r = theme::resolve_style(decls);
      std::ostringstream fs;
      if (r.font_size) fs << *r.font_size;
      else fs << "-";
      got = r.shape + "|" + r.line + "|" + r.text + "|" + (r.stroke ? *r.stroke : "-") + "|" + std::to_string(r.bold) + std::to_string(r.italic) + "|" + fs.str();
    } else if (kind == "series") {
      int i;
      std::string out;
      s >> i >> out;
      want = unhex8(out);
      got = theme::series_color(i);
    } else if (kind == "class") {
      std::string raw, out;
      s >> raw >> out;
      want = unhex8(out);
      got = theme::class_names(unhex16(raw));
    } else {
      continue;
    }
    total++;
    if (got != want) {
      failed++;
      if (shown++ < 6) std::cout << kind << " differs\n  got:  " << got << "\n  want: " << want << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
