// Compares preprocess, cleanup_comments and encode_entities with Pele's TypeScript
// (tests/data/preprocess_cases.txt, made by tools/gen_preprocess_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "errors.h"
#include "js_math.h"
#include "preprocess.h"

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
    std::string kind, src;
    s >> kind >> src;
    std::string want;
    std::getline(s, want);
    want = want.substr(1);
    std::string got;
    const ustring u = unhex16(src);
    if (kind == "pre") {
      try {
        const preprocessed r = preprocess(u);
        got = "ok " + hex16(r.text) + " " + (r.title ? hex16(*r.title) : "-") + " " + ser(r.settings) + " " + hex16(r.with_comments) + " " + std::to_string(r.line_offset);
      } catch (const pele_error& e) {
        got = "err " + hex16(to_utf16(e.what()));
      }
    } else if (kind == "clean") {
      got = hex16(cleanup_comments(u));
    } else if (kind == "enc") {
      got = hex16(encode_entities(u));
    } else {
      continue;
    }
    total++;
    if (got != want) {
      failed++;
      if (shown++ < 4) {
        auto un = [](const std::string& h) { return to_utf8(unhex16(h)); };
        std::cout << kind << " differs\n  src: " << un(src) << "\n";
        if (kind == "pre") std::cout << "  got:  " << got.substr(0, 200) << "\n  want: " << want.substr(0, 200) << "\n";
        else std::cout << "  got:  [" << un(got) << "]\n  want: [" << un(want) << "]\n";
      }
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
