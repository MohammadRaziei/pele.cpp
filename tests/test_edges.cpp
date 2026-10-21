// Compares edge_geometry with Pele's TypeScript (tests/data/edge_cases.txt, made by tools/gen_edge_cases.mjs).
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "edges.h"

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

std::optional<double> opt(const std::string& t) { return t == "undefined" ? std::nullopt : std::optional<double>(std::strtod(t.c_str(), nullptr)); }

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
    if (kind == "route") {
      std::size_t n;
      s >> n;
      std::vector<double> route(n);
      for (double& v : route) s >> v;
      std::string curve, sr, er, d8;
      double st, et;
      s >> curve >> st >> et >> sr >> er >> d8;
      double want[8];
      for (double& v : want) s >> v;
      const edge_path p = edge_geometry::route_path(route, curve == "-" ? "" : curve, st, et, opt(sr), opt(er));
      const double got[8] = {p.sx, p.sy, p.sdx, p.sdy, p.ex, p.ey, p.edx, p.edy};
      if (p.d != unhex8(d8)) why = "path\n  got:  " + p.d + "\n  want: " + unhex8(d8);
      for (int i = 0; why.empty() && i < 8; i++)
        if (!close(got[i], want[i])) why = "endpoint value " + std::to_string(i);
    } else if (kind == "marker") {
      std::string type, col8, out8;
      double x, y, dx, dy, trim;
      s >> type >> x >> y >> dx >> dy >> col8 >> out8 >> trim;
      const std::string got = edge_geometry::marker(type, x, y, dx, dy, unhex8(col8));
      if (got != unhex8(out8)) why = "marker\n  got:  " + got + "\n  want: " + unhex8(out8);
      else if (!close(edge_geometry::marker_trim(type), trim)) why = "marker_trim";
    } else if (kind == "loop") {
      double cx, cy, w, h, base, trim;
      int k, sideways;
      loop_label e;
      std::string d8;
      s >> cx >> cy >> w >> h >> k >> sideways >> base >> trim >> e.label_w >> e.label_h >> d8;
      double want[10];
      for (double& v : want) s >> v;
      const edge_path p = edge_geometry::loop_path(cx, cy, w, h, k, sideways != 0, base, trim, e);
      const double got[10] = {p.sx, p.sy, p.sdx, p.sdy, p.ex, p.ey, p.edx, p.edy, e.label_x, e.label_y};
      if (p.d != unhex8(d8)) why = "loop path\n  got:  " + p.d + "\n  want: " + unhex8(d8);
      for (int i = 0; why.empty() && i < 10; i++)
        if (!close(got[i], want[i])) why = "loop value " + std::to_string(i);
    } else if (kind == "elabel") {
      std::string id8, raw, at8, out8;
      double x, y;
      s >> id8 >> raw >> x >> y >> at8 >> out8;
      const label l = layouter.layout(unhex16(raw), false, 14, 200, 0);
      const std::string got = edge_geometry::label_svg(unhex8(id8), l, x, y, unhex8(at8));
      if (got != unhex8(out8)) why = "elabel\n  got:  " + got + "\n  want: " + unhex8(out8);
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
