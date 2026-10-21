// Compares parse_yaml, parse_json, diagram_detector and locate/syntax_error with Pele's TypeScript
// (tests/data/front_cases.txt, made by tools/gen_front_cases.mjs).
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "detect.h"
#include "errors.h"
#include "js_math.h"
#include "yaml.h"

using namespace pele;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

std::string hex16(ustring_view s) {
  static const char* digits = "0123456789abcdef";
  std::string out = "x";
  for (char16_t c : s) {
    out += digits[(c >> 12) & 15];
    out += digits[(c >> 8) & 15];
    out += digits[(c >> 4) & 15];
    out += digits[c & 15];
  }
  return out;
}

std::string ser(const yaml_value& v) {
  switch (v.type()) {
    case yaml_value::kind::null: return "N";
    case yaml_value::kind::boolean: return v.as_boolean() ? "T" : "F";
    case yaml_value::kind::number: return "n" + js_number_string(v.as_number());
    case yaml_value::kind::string: return "s" + hex16(v.as_string());
    case yaml_value::kind::array: {
      std::string out = "[";
      for (std::size_t i = 0; i < v.items().size(); i++) out += (i ? "," : "") + ser(v.items()[i]);
      return out + "]";
    }
    default: {
      std::string out = "{";
      bool first = true;
      for (const ustring& k : v.keys()) {
        out += (first ? "" : ",") + hex16(k) + "=" + ser(*v.find(k));
        first = false;
      }
      return out + "}";
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string kind;
    s >> kind;
    std::string got, want;
    if (kind == "yaml" || kind == "json") {
      std::string doc;
      s >> doc;
      std::getline(s, want);
      want = want.substr(1);
      try {
        got = "ok " + ser(kind == "yaml" ? parse_yaml(unhex16(doc)) : parse_json(unhex16(doc)));
      } catch (const yaml_error& e) {
        got = kind == "yaml" ? "err " + hex16(to_utf16(e.what())) : "err";
      }
    } else if (kind == "detect") {
      std::string doc, t, m;
      s >> doc >> t >> m;
      want = t + " " + m;
      const auto d = diagram_detector::detect_diagram(unhex16(doc));
      got = d ? std::string(to_string(d->type)) + " " + d->mermaid_type : "- -";
    } else if (kind == "locate") {
      std::string src, snip, detail, m1, m2, code, type;
      std::size_t offset;
      int ln, col;
      s >> src >> offset >> ln >> col >> snip >> detail >> m1 >> m2 >> code >> type;
      want = std::to_string(ln) + ":" + std::to_string(col) + " " + snip + " " + m1 + " " + m2 + " " + code + " " + type;
      const ustring u = unhex16(src);
      const source_location loc = locate(u, offset);
      const pele_error e1 = syntax_error("flowchart", u, offset, unhex16(detail), false);
      const pele_error e2 = syntax_error("pie", u, offset, unhex16(detail), true);
      got = std::to_string(loc.line) + ":" + std::to_string(loc.column) + " " + hex16(loc.snippet) + " " + hex16(to_utf16(e1.what())) + " " + hex16(to_utf16(e2.what())) + " " +
            to_string(e1.code()) + " " + *e1.type();
    } else {
      continue;
    }
    total++;
    if (got != want) {
      failed++;
      if (shown++ < 6) std::cout << kind << " differs\n  got:  " << got.substr(0, 300) << "\n  want: " << want.substr(0, 300) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
