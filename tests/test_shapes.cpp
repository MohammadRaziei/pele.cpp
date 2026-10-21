// Compares shape_registry with Pele's TypeScript (tests/data/shape_cases.txt, made by tools/gen_shape_cases.mjs).
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "shapes.h"

using namespace pele;

namespace {

std::string unhex8(const std::string& s) {
  std::string out;
  for (std::size_t i = 1; i + 2 <= s.size(); i += 2) out += static_cast<char>(std::stoul(s.substr(i, 2), nullptr, 16));
  return out;
}

bool close(double a, double b) { return std::abs(a - b) <= 1e-9 * (1 + std::abs(b)); }

std::vector<double> numbers(const std::string& csv) {
  std::vector<double> out;
  std::stringstream ss(csv);
  std::string t;
  while (std::getline(ss, t, ',')) out.push_back(std::stod(t));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  const shape_registry& shapes = shape_registry::instance();
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string kind, name;
    s >> kind >> name;
    std::string why;
    if (kind == "shape") {
      double tw, th, w, h, dy;
      std::string a8, line8, drawn8, insets, spans;
      int has_label;
      s >> tw >> th >> w >> h >> dy >> a8 >> line8 >> drawn8 >> has_label >> insets >> spans;
      const shape_size sz = shapes.size(name, tw, th);
      if (!close(sz.w, w) || !close(sz.h, h) || !close(sz.dy, dy)) why = name + ": size";
      const std::string drawn = shapes.draw(name, sz.w, sz.h, unhex8(a8), unhex8(line8));
      if (why.empty() && drawn != unhex8(drawn8)) why = name + ": draw\n  got:  " + drawn + "\n  want: " + unhex8(drawn8);
      if (why.empty() && shapes.has_label(name) != (has_label != 0)) why = name + ": has_label";
      if (why.empty()) {
        const std::vector<double> want = numbers(insets);
        int i = 0;
        for (int side = 0; why.empty() && side < 4; side++)
          for (double t : {0.0, 7.5, -12.0, 40.0}) {
            if (!close(shapes.inset(name, sz.w, sz.h, side, t), want[i++])) {
              why = name + ": inset side " + std::to_string(side);
              break;
            }
          }
      }
      if (why.empty()) {
        const std::vector<double> want = numbers(spans);
        if (!close(shapes.span(name, sz.w, sz.h, false), want[0]) || !close(shapes.span(name, sz.w, sz.h, true), want[1])) why = name + ": span";
      }
    } else if (kind == "route") {
      compound_node c;
      int group, side;
      shape_view view;
      view.shape = name;
      s >> c.w >> c.h >> c.x >> c.y >> group >> view.w >> view.h;
      c.is_group = group != 0;
      std::vector<double> route(3), want(3);
      for (double& v : route) s >> v;
      s >> side;
      for (double& v : want) s >> v;
      if (end_side(route, 0, c) != side) why = name + ": end_side";
      inset_route(route, 0, view, c);
      for (int i = 0; why.empty() && i < 3; i++)
        if (!close(route[i], want[i])) why = name + ": inset_route";
    } else {
      continue;
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 5) std::cout << why << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
