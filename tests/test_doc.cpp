// Compares svg_document, with_title and the cluster helpers with Pele's TypeScript
// (tests/data/doc_cases.txt, made by tools/gen_doc_cases.mjs).
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "cluster.h"
#include "svg_document.h"
#include "title.h"

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

bool close(double a, double b) { return std::abs(a - b) <= 1e-9 * (1 + std::abs(b)); }

std::optional<std::string> text8(const std::string& t) { return t == "-" ? std::nullopt : std::optional<std::string>(unhex8(t)); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  const metrics_measurer measurer;
  const label_layouter layouter(measurer);
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string kind;
    s >> kind;
    std::string why;
    if (kind == "doc") {
      std::string type, prefix, responsive, fam, mono, acc_title, acc_descr, content8, out8;
      double w, h, fs;
      s >> type >> w >> h >> fs >> prefix >> responsive >> fam >> mono >> acc_title >> acc_descr >> content8 >> out8;
      render_options o;
      o.id_prefix = text8(prefix);
      o.responsive = responsive != "false";
      o.font_family = text8(fam);
      o.font_family_mono = text8(mono);
      accessible acc;
      if (acc_title != "-") acc.title = unhex16(acc_title);
      if (acc_descr != "-") acc.description = unhex16(acc_descr);
      const std::string got = svg_document::build(type, w, h, fs, o, acc, unhex8(content8));
      if (got != unhex8(out8)) why = "doc\n  got:  " + got + "\n  want: " + unhex8(out8);
    } else if (kind == "title") {
      std::string title, content8, out8;
      double size, pad, w, h, rw, rh;
      s >> title >> size >> pad >> w >> h >> content8 >> rw >> rh >> out8;
      const titled t = with_title(title == "-" ? ustring() : unhex16(title), layouter, size, pad, w, h, unhex8(content8));
      if (!close(t.width, rw) || !close(t.height, rh)) why = "title size";
      else if (t.content != unhex8(out8)) why = "title\n  got:  " + t.content + "\n  want: " + unhex8(out8);
    } else if (kind == "cluster") {
      std::size_t nroutes;
      s >> nroutes;
      crossing_map map;
      for (std::size_t i = 0; i < nroutes; i++) {
        std::size_t n;
        s >> n;
        std::vector<double> route(n);
        for (double& v : route) s >> v;
        map.mark(route);
      }
      compound_node node;
      double width, pad, tx, xtest;
      int crossed, depth;
      std::string out8;
      s >> node.w >> node.h >> node.x >> node.y >> width >> pad >> tx >> xtest >> crossed >> depth >> out8;
      const double got_x = map.title_x(node, width, pad);
      if (!close(got_x, tx)) why = "title_x " + std::to_string(got_x) + " vs " + std::to_string(tx);
      else if (map.crossed(node, width, xtest) != (crossed != 0)) why = "crossed";
      else {
        std::vector<compound_node> nodes(depth + 1);
        for (int i = 0; i <= depth; i++) nodes[i].parent = i < depth ? i + 1 : -1;
        const label l = layouter.layout(u"Cluster", false, 12, 200, 0);
        const std::string got = struck_title(l, 10.5, 20, " class=\"t\"", "c1", nodes, 0);
        if (got != unhex8(out8)) why = "struck\n  got:  " + got + "\n  want: " + unhex8(out8);
      }
    } else {
      continue;
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 5) std::cout << kind << " differs: " << why << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
