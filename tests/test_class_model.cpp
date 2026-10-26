// Compares the class diagram parser and db with Pele's TypeScript, by the text of the model they
// build or the error they give (tests/data/class_model_cases.txt, made by tools/gen_class_model_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "class/parser.h"
#include "errors.h"

using namespace pele;
using namespace pele::classdiagram;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

// A string as length:text, nothing as -, a list as count[items]: the text the generator makes.
ustring f(const ustring& s) { return to_utf16(std::to_string(s.size())) + u":" + s; }
ustring f(const std::optional<ustring>& s) { return s ? f(*s) : ustring(u"-"); }
ustring list(const std::vector<ustring>& a) {
  ustring out = to_utf16(std::to_string(a.size())) + u"[";
  for (const ustring& s : a) out += f(s);
  return out + u"]";
}
ustring number(int v) { return to_utf16(v == kEndNone ? std::string("none") : std::to_string(v)); }

ustring member(const class_member& m) {
  return u"(" + f(ustring(m.member_type == class_member::kind::method ? u"method" : u"attribute")) + f(m.visibility) + f(m.classifier) + f(m.id) + f(m.parameters) + f(m.return_type) + u")";
}

ustring dump(const class_model& m) {
  ustring s = u"D" + f(m.direction) + f(m.title) + f(m.acc_title) + f(m.acc_descr);
  for (const class_node& c : m.classes) {
    s += u"C" + f(c.id) + f(c.type) + f(c.label) + f(c.css_classes) + list(c.annotations) + list(c.styles) + f(c.parent) + f(c.link) + f(c.link_target) + (c.have_callback ? u"1" : u"0") + f(c.tooltip);
    s += to_utf16(std::to_string(c.members.size()));
    for (const class_member& x : c.members) s += member(x);
    s += to_utf16(std::to_string(c.methods.size()));
    for (const class_member& x : c.methods) s += member(x);
    s += u";";
  }
  for (const class_relation& r : m.relations) s += u"R" + f(r.id1) + f(r.id2) + f(r.relation_title1) + f(r.relation_title2) + f(r.title) + number(r.type1) + u"," + number(r.type2) + u"," + number(r.line_type) + u";";
  for (const class_note& n : m.notes) s += u"N" + f(n.id) + f(n.cls) + f(n.text) + to_utf16(std::to_string(n.index)) + f(n.parent) + u";";
  for (const namespace_node& n : m.namespaces) {
    std::vector<ustring> classes, notes, children;
    for (int i : n.classes) classes.push_back(m.classes[static_cast<std::size_t>(i)].id);
    for (int i : n.notes) notes.push_back(m.notes[static_cast<std::size_t>(i)].id);
    for (int i : n.children) children.push_back(m.namespaces[static_cast<std::size_t>(i)].id);
    s += u"S" + f(n.id) + f(n.label) + list(classes) + list(notes) + list(children) + f(n.parent) + (n.explicit_ ? u"1" : u"0") + u";";
  }
  for (const class_interface& i : m.interfaces) s += u"I" + f(i.id) + f(i.label) + f(i.class_id) + u";";
  for (const style_class& y : m.style_classes) s += u"Y" + f(y.id) + list(y.styles) + list(y.text_styles) + u";";
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string tag, src, kind, hex;
    s >> tag >> src >> kind >> hex;
    const ustring text = unhex16(src);
    const ustring want = unhex16(hex);
    ustring got;
    std::string how;
    try {
      class_db db;
      parse_class_diagram(text, db);
      got = dump(db.model);
      how = "ok";
    } catch (const pele_error& e) {
      got = to_utf16(e.what());
      how = "err";
    }
    total++;
    if (how != kind || got != want) {
      failed++;
      if (shown++ < 4) {
        std::size_t at = 0;
        while (at < got.size() && at < want.size() && got[at] == want[at]) at++;
        std::cout << "differs (" << how << " vs " << kind << ")\n  got:  " << to_utf8(got.substr(at > 30 ? at - 30 : 0, 110)) << "\n  want: " << to_utf8(want.substr(at > 30 ? at - 30 : 0, 110)) << "\n  source: " << to_utf8(text).substr(0, 140) << "\n";
      }
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 && total > 0 ? 0 : 1;
}
