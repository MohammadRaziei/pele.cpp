// Compares js_to_fixed, js_to_exponential and js_to_precision with JavaScript's
// (tests/data/format_cases.txt, made by tools/gen_format_cases.mjs).
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "js_format.h"
#include "js_math.h"
#include "js_parse.h"

using namespace pele;

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    if (line[0] == 'p') {
      std::string tag, src, want;
      s >> tag >> src >> want;
      ustring text;
      for (std::size_t i = 1; i + 4 <= src.size(); i += 4) text += static_cast<char16_t>(std::stoul(src.substr(i, 4), nullptr, 16));
      const double got = js_parse_float(text);
      const std::string shown_got = got == 0 && std::signbit(got) ? "-0" : js_number_string(got);
      total++;
      if (shown_got != want) {
        failed++;
        if (shown++ < 8) std::cout << "parseFloat differs: " << shown_got << " vs " << want << " for " << to_utf8(text) << "\n";
      }
      continue;
    }
    std::string tag, hex, fixed, expo, prec;
    int f, p;
    s >> tag >> hex >> f >> p >> fixed >> expo >> prec;
    unsigned long long bits = std::stoull(hex, nullptr, 16);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    const std::string a = js_to_fixed(v, f), b = js_to_exponential(v, f), c = js_to_precision(v, p);
    total++;
    if (a != fixed || b != expo || c != prec) {
      failed++;
      if (shown++ < 8) std::cout << "differs for " << hex << " (f=" << f << ", p=" << p << ")\n  toFixed " << a << " vs " << fixed << "\n  toExponential " << b << " vs " << expo << "\n  toPrecision " << c << " vs " << prec << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " numbers match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}
