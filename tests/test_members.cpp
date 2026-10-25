// Compares parse_generic_types and class_member with Pele's TypeScript (tests/data/members_cases.txt,
// made by tools/gen_members_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "class/members.h"
#include "common/generics.h"

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
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string tag, why;
    s >> tag;
    ustring text;
    if (tag == "g") {
      std::string src, want;
      s >> src >> want;
      text = unhex16(src);
      if (parse_generic_types(text) != unhex16(want)) why = "generics: " + to_utf8(parse_generic_types(text)) + " vs " + to_utf8(unhex16(want));
    } else if (tag == "m") {
      std::string type, src, kind, vis, cls, id, params, ret, shown_text, css;
      s >> type >> src >> kind >> vis >> cls >> id >> params >> ret >> shown_text >> css;
      text = unhex16(src);
      const classdiagram::class_member m(text, type == "method" ? classdiagram::class_member::kind::method : classdiagram::class_member::kind::attribute);
      const auto d = m.display_details();
      const std::string got_kind = m.member_type == classdiagram::class_member::kind::method ? "method" : "attribute";
      if (got_kind != kind) why = type + ": kind " + got_kind + " vs " + kind;
      else if (m.visibility != unhex16(vis)) why = type + ": visibility";
      else if (m.classifier != unhex16(cls)) why = type + ": classifier";
      else if (m.id != unhex16(id)) why = type + ": id [" + to_utf8(m.id) + "] vs [" + to_utf8(unhex16(id)) + "]";
      else if (m.parameters != unhex16(params)) why = type + ": parameters";
      else if (m.return_type != unhex16(ret)) why = type + ": return type";
      else if (d.text != unhex16(shown_text)) why = type + ": display [" + to_utf8(d.text) + "] vs [" + to_utf8(unhex16(shown_text)) + "]";
      else if (to_utf16(d.css_style) != unhex16(css)) why = type + ": css";
    } else {
      continue;
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 6) std::cout << "differs: " << why << "\n  input: " << to_utf8(text).substr(0, 80) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}
