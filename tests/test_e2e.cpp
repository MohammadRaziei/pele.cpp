// Renders diagrams with pele::render and compares each with what Pele's TypeScript gave for it, or
// checks that it fails the same way. The argument is either
//   - a manifest made by tools/pack_e2e.mjs: one case per line, with the SHA-256 of the SVG, or
//   - a directory of N.mmd with N.svg or N.err (the code, then the message) and N.opts, as the
//     gen_*_cases.mjs tools write them, which also shows what differs in an SVG.
#include <pele/pele.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "sha256.h"

namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The options a case was made with, one `name=number` per line in N.opts (maxWidth, fontSize, padding).
pele::render_options read_options(const fs::path& src) {
  pele::render_options options;
  fs::path path = src;
  path.replace_extension(".opts");
  if (!fs::exists(path)) return options;
  std::istringstream lines(read(path));
  std::string line;
  while (std::getline(lines, line)) {
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const double value = std::stod(line.substr(eq + 1));
    if (key == "maxWidth") options.max_width = value;
    else if (key == "fontSize") options.font_size = value;
    else if (key == "padding") options.padding = value;
  }
  return options;
}

std::string unescape(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (s[i] != '\\' || i + 1 == s.size()) {
      out += s[i];
      continue;
    }
    const char c = s[++i];
    out += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
  }
  return out;
}

pele::render_options parse_options(const std::string& text) {
  pele::render_options options;
  std::istringstream parts(text);
  std::string part;
  while (std::getline(parts, part, ';')) {
    const std::size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = part.substr(0, eq);
    const double value = std::stod(part.substr(eq + 1));
    if (key == "maxWidth") options.max_width = value;
    else if (key == "fontSize") options.font_size = value;
    else if (key == "padding") options.padding = value;
  }
  return options;
}

int run_manifest(const char* path) {
  std::ifstream in(path, std::ios::binary);
  std::string line;
  int total = 0, failed = 0;
  while (std::getline(in, line)) {
    // A checkout that converted line endings leaves a \r here; real ones are escaped in the data.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    std::vector<std::string> f;
    std::size_t from = 0;
    for (int i = 0; i < 3; i++) {
      const std::size_t tab = line.find('\t', from);
      f.push_back(line.substr(from, tab - from));
      from = tab + 1;
    }
    f.push_back(line.substr(from));
    const std::string text = unescape(f[2]);
    std::string why;
    try {
      const pele::render_result r = pele::render(text, f[1] == "-" ? pele::render_options() : parse_options(f[1]));
      if (f[3].rfind("svg:", 0) != 0) why = "should have failed";
      else if (pele_test::sha256_hex(r.svg) != f[3].substr(4)) why = "svg differs";
    } catch (const pele::pele_error& e) {
      if (f[3].rfind("err:", 0) != 0) why = std::string("unexpected error: ") + e.what();
      else {
        const std::string want = unescape(f[3].substr(4));
        const std::string got = std::string(pele::to_string(e.code())) + "\n" + e.what();
        if (got != want) why = "error differs:\n  got:  " + got + "\n  want: " + want;
      }
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (failed <= 5) std::cout << f[0] << ": " << why << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " diagrams match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  if (!fs::is_directory(argv[1])) return run_manifest(argv[1]);
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
      const pele::render_result r = pele::render(text, read_options(src));
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
