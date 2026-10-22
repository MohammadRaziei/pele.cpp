// Renders each tests/data/e2e/N.mmd with pele::render and compares it with what Pele's TypeScript
// gave for it (N.svg), or checks that it fails the same way (N.err: the code, then the message).
#include <pele/pele.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::vector<fs::path> sources;
  for (const auto& entry : fs::directory_iterator(argv[1]))
    if (entry.path().extension() == ".mmd") sources.push_back(entry.path());
  std::sort(sources.begin(), sources.end());
  int total = 0, failed = 0;
  for (const fs::path& src : sources) {
    fs::path svg = src, err = src;
    svg.replace_extension(".svg");
    err.replace_extension(".err");
    const std::string text = read(src);
    std::string why;
    try {
      const pele::render_result r = pele::render(text);
      if (!fs::exists(svg)) why = "should have failed";
      else if (r.svg != read(svg)) why = "svg differs";
    } catch (const pele::pele_error& e) {
      if (!fs::exists(err)) why = std::string("unexpected error: ") + e.what();
      else {
        const std::string want = read(err);
        const std::string got = std::string(pele::to_string(e.code())) + "\n" + e.what();
        if (got != want) why = "error differs:\n  got:  " + got + "\n  want: " + want;
      }
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (failed <= 5) std::cout << src.filename().string() << ": " << why << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " diagrams match\n";
  return failed == 0 ? 0 : 1;
}
