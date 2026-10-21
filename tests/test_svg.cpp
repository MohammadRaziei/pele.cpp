// Compares the helpers in svg.h with Pele's TypeScript (tests/data/svg_cases.txt, made by tools/gen_svg_cases.mjs).
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "svg.h"

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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  const metrics_measurer measurer;
  const icon_set icons([](const std::string& n) -> std::optional<std::string> {
    if (n == "fa:fa-car" || n == "fab:fa-github") return std::string("<path d=\"M0 0L1 1\"/>");
    return std::nullopt;
  });
  const label_layouter layouter(measurer, &icons);
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string kind;
    s >> kind;
    std::string got, want;
    if (kind == "num") {
      std::string v;
      s >> v >> want;
      got = svg::num(std::strtod(v.c_str(), nullptr));
    } else if (kind == "esc" || kind == "esct") {
      std::string in16, out8;
      s >> in16 >> out8;
      want = unhex8(out8);
      got = kind == "esc" ? svg::esc(unhex16(in16)) : svg::esc_text(unhex16(in16));
    } else if (kind == "lab") {
      int md, base;
      double size, max_width, cx, cy;
      std::string attrs, raw, out8;
      s >> md >> size >> max_width >> base >> cx >> cy >> attrs >> raw >> out8;
      want = unhex8(out8);
      got = svg::label_svg(layouter.layout(unhex16(raw), md != 0, size, max_width, base), cx, cy, unhex8(attrs), &icons);
    } else {
      continue;
    }
    total++;
    if (got != want) {
      failed++;
      if (shown++ < 6) std::cout << kind << " differs\n  got:  " << got << "\n  want: " << want << "\n  line: " << line.substr(0, 120) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
