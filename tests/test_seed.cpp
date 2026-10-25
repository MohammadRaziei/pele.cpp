// Compares cynefin's seeded_random, hash_string and wave with Pele's TypeScript, bit for bit
// (tests/data/seed_cases.txt, made by tools/gen_seed_cases.mjs).
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "cynefin/boundaries.h"

using namespace pele;

namespace {

double from_bits(const std::string& hex) {
  const unsigned long long bits = std::stoull(hex, nullptr, 16);
  double v;
  std::memcpy(&v, &bits, sizeof v);
  return v;
}

bool same(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0 || (a != a && b != b); }

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string part;
  while (std::getline(in, part, sep)) out.push_back(part);
  return out;
}

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string tag;
    s >> tag;
    std::string why;
    if (tag == "r") {
      std::string a, b;
      s >> a >> b;
      if (!same(cynefin::seeded_random(from_bits(a)), from_bits(b))) why = "seeded_random " + a;
    } else if (tag == "h") {
      std::string a;
      long long want;
      s >> a >> want;
      if (cynefin::hash_string(unhex16(a)) != want) why = "hash_string " + std::to_string(cynefin::hash_string(unhex16(a))) + " vs " + std::to_string(want);
    } else if (tag == "w") {
      std::string args, flags_a, flags_b, flags_c, flags_d, curve;
      int segments, vertical, pin_start, pin_end;
      s >> args >> segments >> vertical >> pin_start >> pin_end >> curve;
      const auto a = split(args, ',');
      cynefin::wave_options o = vertical ? cynefin::fold_options() : cynefin::horizon_options();
      o.segments = segments;
      o.pin_start = pin_start != 0;
      o.pin_end = pin_end != 0;
      const std::vector<double> got = cynefin::wave(from_bits(a[0]), from_bits(a[1]), from_bits(a[2]), from_bits(a[3]), from_bits(a[4]), o);
      const auto want = split(curve, ',');
      if (got.size() != want.size()) why = "wave length";
      for (std::size_t i = 0; why.empty() && i < got.size(); i++)
        if (!same(got[i], from_bits(want[i]))) why = "wave point " + std::to_string(i);
    } else {
      continue;
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 6) std::cout << "differs: " << why << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}
