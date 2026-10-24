// Compares literal_label and fit_text with Pele's TypeScript (tests/data/text_cases.txt, made by
// tools/gen_text_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "common/fit_text.h"
#include "common/literal_label.h"
#include "js_math.h"
#include "text_measurer.h"

using namespace pele;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  const metrics_measurer measurer;
  std::string line;
  int total = 0, failed = 0, shown = 0;
  const auto report = [&](const std::string& why, const ustring& text) {
    failed++;
    if (shown++ < 6) std::cout << "differs: " << why << "\n  text: " << to_utf8(text).substr(0, 100) << "\n";
  };
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string tag, src;
    s >> tag >> src;
    if (tag == "l") {
      std::string size, room, width, height, line_height;
      int style;
      std::size_t count;
      s >> size >> room >> style >> count >> width >> height >> line_height;
      std::vector<std::pair<std::string, std::string>> want(count);
      for (auto& w : want) {
        std::getline(in, line);
        std::istringstream ls(line);
        ls >> w.first >> w.second;
      }
      total++;
      const ustring text = unhex16(src);
      const label got = literal_label(text, measurer, std::stod(size), std::stod(room), style);
      std::string why;
      if (got.lines.size() != count) why = "line count " + std::to_string(got.lines.size()) + " vs " + std::to_string(count);
      else if (js_number_string(got.width) != width || js_number_string(got.height) != height || js_number_string(got.line_height) != line_height) why = "size " + js_number_string(got.width) + "x" + js_number_string(got.height) + " vs " + width + "x" + height;
      for (std::size_t i = 0; why.empty() && i < count; i++) {
        if (js_number_string(got.widths[i]) != want[i].first || got.lines[i].size() != 1 || got.lines[i][0].text != unhex16(want[i].second) || got.lines[i][0].style != style ||
            js_number_string(got.lines[i][0].width) != want[i].first)
          why = "line " + std::to_string(i);
      }
      if (!why.empty()) report("literal_label " + why, text);
    } else if (tag == "f") {
      std::string max, size, min, rest;
      int style;
      s >> max >> size >> min >> style;
      std::getline(s, rest);
      if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
      total++;
      const ustring text = unhex16(src);
      const auto got = fit_text(text, std::stod(max), std::stod(size), std::stod(min), measurer, style);
      std::string have = "-";
      if (got) have = js_number_string(got->size) + " " + js_number_string(got->width) + " x";
      std::string want = rest;
      std::string why;
      if (!got) {
        if (rest != "-") why = "expected a fit";
      } else {
        std::istringstream ws(rest);
        std::string wsize, wwidth, wtext;
        ws >> wsize >> wwidth >> wtext;
        if (rest == "-") why = "expected nothing";
        else if (js_number_string(got->size) != wsize || js_number_string(got->width) != wwidth || got->text != unhex16(wtext)) why = "fit " + have + " vs " + rest.substr(0, 30);
      }
      if (!why.empty()) report("fit_text " + why, text);
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
