// Compares layered_layout with the results of Pele's TypeScript layout on the same input
// (tests/data/layered_cases.txt, made by tools/gen_layered_cases.mjs).
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "layered.h"

using namespace pele;

namespace {

bool close(double a, double b) { return std::abs(a - b) <= 1e-6 * (1 + std::abs(b)); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: test_layered cases.txt\n";
    return 2;
  }
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0;
  int failed = 0;
  int shown = 0;
  while (std::getline(in, line)) {
    if (line.rfind("case ", 0) != 0) continue;
    const int id = std::stoi(line.substr(5));
    layered_options opt;
    std::getline(in, line);
    {
      std::istringstream s(line.substr(5));
      int tracks, straight, apart;
      s >> opt.node_sep >> opt.edge_sep >> opt.rank_sep >> opt.port_sep >> tracks >> straight >> apart >> opt.head_room;
      opt.tracks = tracks != 0;
      opt.straight = straight != 0;
      opt.apart = apart != 0;
    }
    std::getline(in, line);
    const int n = std::stoi(line.substr(6));
    std::vector<layered_node> nodes;
    for (int i = 0; i < n; i++) {
      std::getline(in, line);
      std::istringstream s(line);
      layered_node nd;
      int kind;
      s >> nd.w >> nd.h >> kind >> nd.pin >> nd.span;
      nd.kind = static_cast<node_kind>(kind);
      nodes.push_back(nd);
    }
    std::getline(in, line);
    const int m = std::stoi(line.substr(6));
    std::vector<layered_edge> edges;
    for (int i = 0; i < m; i++) {
      std::getline(in, line);
      std::istringstream s(line);
      layered_edge e;
      int tf, hf;
      s >> e.tail >> e.head >> e.minlen >> e.label_w >> e.label_h >> e.tail_dx >> e.head_dx >> tf >> hf;
      e.tail_fixed = tf != 0;
      e.head_fixed = hf != 0;
      edges.push_back(e);
    }
    std::getline(in, line);
    if (line == "error") continue;
    double rw, rh;
    {
      std::istringstream s(line.substr(7));
      s >> rw >> rh;
    }
    std::vector<std::vector<double>> expect_n;
    for (int i = 0; i < n; i++) {
      std::getline(in, line);
      std::istringstream s(line.substr(2));
      double x, y, r;
      s >> x >> y >> r;
      expect_n.push_back({x, y, r});
    }
    struct exp_e {
      int reversed;
      double lx, ly, tdx, hdx;
      std::vector<double> pts;
    };
    std::vector<exp_e> expect_e;
    for (int i = 0; i < m; i++) {
      std::getline(in, line);
      std::istringstream s(line.substr(2));
      exp_e e;
      int np;
      s >> e.reversed >> e.lx >> e.ly >> e.tdx >> e.hdx >> np;
      e.pts.resize(np);
      for (int k = 0; k < np; k++) s >> e.pts[k];
      expect_e.push_back(e);
    }
    std::getline(in, line);  // end

    total++;
    const layered_result res = layered_layout(nodes, edges, opt);
    std::string why;
    if (!close(res.width, rw) || !close(res.height, rh)) why = "size";
    for (int i = 0; why.empty() && i < n; i++)
      if (!close(nodes[i].x, expect_n[i][0]) || !close(nodes[i].y, expect_n[i][1]) || nodes[i].rank != static_cast<int>(expect_n[i][2]))
        why = "node " + std::to_string(i);
    for (int i = 0; why.empty() && i < m; i++) {
      const exp_e& x = expect_e[i];
      const layered_edge& e = edges[i];
      if (static_cast<int>(e.reversed) != x.reversed) why = "reversed " + std::to_string(i);
      else if (e.points.size() != x.pts.size()) why = "npoints " + std::to_string(i);
      else
        for (std::size_t k = 0; k < x.pts.size(); k++)
          if (!close(e.points[k], x.pts[k])) why = "point " + std::to_string(i) + "/" + std::to_string(k);
      if (why.empty() && (!close(e.label_x, x.lx) || !close(e.label_y, x.ly))) why = "label " + std::to_string(i);
      if (why.empty() && (!close(e.tail_dx, x.tdx) || !close(e.head_dx, x.hdx))) why = "dx " + std::to_string(i);
    }
    if (!why.empty()) {
      failed++;
      if (shown++ < 8) std::cout << "case " << id << " differs: " << why << " (n=" << n << " m=" << m << ")\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
