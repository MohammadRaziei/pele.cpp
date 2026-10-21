// Compares label_layouter and decode_entities with Pele's TypeScript on the same text
// (tests/data/label_cases.txt, made by tools/gen_label_cases.mjs).
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "entities.h"
#include "label.h"

using namespace pele;

namespace {

bool close(double a, double b) { return std::abs(a - b) <= 1e-9 * (1 + std::abs(b)); }

ustring unhex(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 3 < s.size() + 0 && i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  const metrics_measurer measurer;
  const icon_set icons([](const std::string& n) -> std::optional<std::string> {
    if (n == "fa:fa-car" || n == "fab:fa-github" || n == "fas:fa-star") return std::string("<svg/>");
    return std::nullopt;
  });
  const label_layouter layouter(measurer, &icons);
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    if (line.rfind("case ", 0) != 0) continue;
    std::istringstream head(line.substr(5));
    int id, md, base;
    double size, max_width;
    std::string raw_hex, decoded_hex;
    head >> id >> md >> size >> max_width >> base >> raw_hex >> decoded_hex;
    std::getline(in, line);
    std::istringstream lh(line.substr(6));
    double ew, eh, es, elh;
    std::size_t nlines;
    lh >> ew >> eh >> es >> elh >> nlines;
    struct esp { int style; std::string icon; double w; ustring text; };
    std::vector<std::vector<esp>> el;
    std::vector<double> ewidths;
    for (std::size_t i = 0; i < nlines; i++) {
      std::getline(in, line);
      std::istringstream l(line.substr(5));
      double w;
      std::size_t ns;
      l >> w >> ns;
      ewidths.push_back(w);
      el.emplace_back();
      for (std::size_t k = 0; k < ns; k++) {
        std::getline(in, line);
        std::istringstream sp(line.substr(5));
        esp e;
        std::string th;
        sp >> e.style >> e.icon >> e.w >> th;
        e.text = unhex(th);
        el.back().push_back(e);
      }
    }
    total++;
    const ustring raw = unhex(raw_hex);
    const label got = layouter.layout(raw, md != 0, size, max_width, base);
    std::string why;
    if (decode_entities(raw) != unhex(decoded_hex)) why = "decode_entities";
    else if (!close(got.width, ew) || !close(got.height, eh) || !close(got.size, es) || !close(got.line_height, elh)) why = "label size";
    else if (got.lines.size() != nlines) why = "line count " + std::to_string(got.lines.size()) + " vs " + std::to_string(nlines);
    for (std::size_t i = 0; why.empty() && i < nlines; i++) {
      if (!close(got.widths[i], ewidths[i])) why = "line width " + std::to_string(i);
      else if (got.lines[i].size() != el[i].size()) why = "span count line " + std::to_string(i);
      else
        for (std::size_t k = 0; why.empty() && k < el[i].size(); k++) {
          const span& s = got.lines[i][k];
          const esp& e = el[i][k];
          if (s.style != e.style) why = "style";
          else if ((s.has_icon ? s.icon : std::string("-")) != e.icon) why = "icon";
          else if (!close(s.width, e.w)) why = "span width";
          else if (s.text != e.text) why = "span text";
        }
    }
    if (!why.empty()) {
      failed++;
      if (shown++ < 8) std::cout << "case " << id << " differs: " << why << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
