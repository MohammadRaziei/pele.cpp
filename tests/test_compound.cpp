// Compares compound_layouter with Pele's TypeScript on the same input
// (tests/data/compound_cases.txt, made by tools/gen_compound_cases.mjs).
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "compound.h"

using namespace pele;

namespace {
bool close(double a, double b) { return std::abs(a - b) <= 1e-6 * (1 + std::abs(b)); }
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    if (line.rfind("case ", 0) != 0) continue;
    const int id = std::stoi(line.substr(5));
    layered_options opt;
    std::string root_dir;
    std::getline(in, line);
    {
      std::istringstream s(line.substr(5));
      int tracks, straight, apart;
      s >> opt.node_sep >> opt.edge_sep >> opt.rank_sep >> opt.port_sep >> tracks >> straight >> apart >> opt.head_room >> root_dir;
      opt.tracks = tracks != 0;
      opt.straight = straight != 0;
      opt.apart = apart != 0;
    }
    std::getline(in, line);
    const int n = std::stoi(line.substr(6));
    std::vector<compound_node> nodes(n);
    for (int i = 0; i < n; i++) {
      std::getline(in, line);
      std::istringstream s(line);
      std::string dir;
      int group;
      s >> nodes[i].w >> nodes[i].h >> nodes[i].parent >> group >> dir >> nodes[i].pad_x >> nodes[i].pad_top >> nodes[i].pad_bottom >> nodes[i].min_w >> nodes[i].seq >> nodes[i].span;
      nodes[i].is_group = group != 0;
      nodes[i].has_dir = dir != "-";
      if (nodes[i].has_dir) nodes[i].dir = parse_direction(dir);
    }
    std::getline(in, line);
    const int m = std::stoi(line.substr(6));
    std::vector<compound_edge> edges(m);
    for (int i = 0; i < m; i++) {
      std::getline(in, line);
      std::istringstream s(line);
      s >> edges[i].src >> edges[i].dst >> edges[i].minlen >> edges[i].label_w >> edges[i].label_h;
    }
    std::getline(in, line);
    if (line == "error") continue;
    double rw, rh;
    { std::istringstream s(line.substr(7)); s >> rw >> rh; }
    std::vector<std::vector<double>> en;
    for (int i = 0; i < n; i++) {
      std::getline(in, line);
      std::istringstream s(line.substr(2));
      double x, y, w, h;
      s >> x >> y >> w >> h;
      en.push_back({x, y, w, h});
    }
    struct ee { double lx, ly; std::vector<double> r; };
    std::vector<ee> ex;
    for (int i = 0; i < m; i++) {
      std::getline(in, line);
      std::istringstream s(line.substr(2));
      ee e; int np;
      s >> e.lx >> e.ly >> np;
      e.r.resize(np);
      for (int k = 0; k < np; k++) s >> e.r[k];
      ex.push_back(e);
    }
    std::getline(in, line);

    total++;
    const layered_layouter engine(opt);
    const compound_result res = compound_layouter(engine).layout(nodes, edges, parse_direction(root_dir));
    std::string why;
    if (!close(res.width, rw) || !close(res.height, rh)) why = "size";
    for (int i = 0; why.empty() && i < n; i++)
      if (!close(nodes[i].x, en[i][0]) || !close(nodes[i].y, en[i][1]) || !close(nodes[i].w, en[i][2]) || !close(nodes[i].h, en[i][3])) why = "node " + std::to_string(i);
    for (int i = 0; why.empty() && i < m; i++) {
      if (edges[i].route.size() != ex[i].r.size()) why = "route length " + std::to_string(i);
      else
        for (std::size_t k = 0; why.empty() && k < ex[i].r.size(); k++)
          if (!close(edges[i].route[k], ex[i].r[k])) why = "route " + std::to_string(i) + "/" + std::to_string(k);
      if (why.empty() && (!close(edges[i].label_x, ex[i].lx) || !close(edges[i].label_y, ex[i].ly))) why = "label " + std::to_string(i);
    }
    if (!why.empty()) {
      failed++;
      if (shown++ < 8) std::cout << "case " << id << " differs: " << why << " (n=" << n << " m=" << m << ")\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
