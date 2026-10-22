// Compares parse_flowchart and flow_db with Pele's TypeScript (tests/data/parser_cases.txt, made by tools/gen_parser_cases.mjs).
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "errors.h"
#include "flowchart/parser.h"
#include "js_math.h"

using namespace pele;
using namespace pele::flowchart;

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

std::string h(const std::optional<ustring>& v) { return v ? hex16(*v) : "-"; }
std::string h8(const std::optional<std::string>& v) { return v ? hex16(to_utf16(*v)) : "-"; }
std::string num(const std::optional<double>& v) { return v ? js_number_string(*v) : "-"; }

std::string list(const std::vector<ustring>& a) {
  std::string out = std::to_string(a.size());
  for (const ustring& x : a) out += " " + hex16(x);
  return out;
}
std::string list_opt(const std::optional<std::vector<ustring>>& a) { return a ? list(*a) : "-"; }

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

std::string label_name(label_type t) { return t == label_type::text ? "text" : t == label_type::string ? "string" : "markdown"; }

std::vector<std::string> dump(const flow_db& db) {
  std::vector<std::string> lines;
  lines.push_back("dir " + h8(db.direction));
  for (const flow_node& n : db.nodes()) {
    std::string props = "-";
    if (!n.props.empty()) {
      props.clear();
      for (std::size_t i = 0; i < n.props.size(); i++) props += (i ? "," : "") + hex16(n.props[i].first) + "=" + hex16(n.props[i].second);
    } else {
      props = "=";
    }
    lines.push_back("node " + hex16(n.id) + " " + h(n.text) + " " + label_name(n.label) + " " + h8(n.type) + " " + list(n.styles) + " " + list(n.classes) + " " + h8(n.dir) + " " +
                    props + " " + h(n.link) + " " + h(n.link_target) + " " + h(n.icon) + " " + h(n.form) + " " + h(n.pos) + " " + h(n.img) + " " + h(n.constraint) + " " +
                    num(n.asset_width) + " " + num(n.asset_height));
  }
  for (const flow_edge& e : db.edges)
    lines.push_back("edge " + hex16(e.start) + " " + hex16(e.end) + " " + h8(e.type) + " " + hex16(e.text) + " " + label_name(e.label) + " " + h8(e.stroke) + " " + num(e.length) +
                    " " + list_opt(e.style) + " " + list(e.classes) + " " + h(e.id) + " " + std::to_string(e.is_user_defined_id ? 1 : 0) + " " + h8(e.interpolate) + " " +
                    (e.animate ? ser(*e.animate) : "-") + " " + (e.animation ? ser(*e.animation) : "-"));
  for (const flow_subgraph& s : db.subgraphs)
    lines.push_back("sub " + hex16(s.id) + " " + hex16(s.title) + " " + label_name(s.label) + " " + h8(s.dir) + " " + list(s.nodes) + " " + list(s.classes) + " " +
                    (s.has_metadata ? ser(s.metadata) : "-"));
  std::vector<std::string> classes;
  for (const auto& kv : db.classes()) classes.push_back("class " + hex16(kv.second.id) + " " + list(kv.second.styles) + " " + list(kv.second.text_styles));
  std::vector<std::string> tips;
  for (const auto& kv : db.tooltips()) tips.push_back("tip " + hex16(kv.first) + " " + hex16(kv.second));
  std::sort(classes.begin(), classes.end());
  std::sort(tips.begin(), tips.end());
  lines.insert(lines.end(), classes.begin(), classes.end());
  lines.insert(lines.end(), tips.begin(), tips.end());
  lines.push_back("misc " + list_opt(db.default_edge_style) + " " + h8(db.default_interpolate) + " " + h(db.acc_title) + " " + h(db.acc_descr));
  return lines;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    if (line.rfind("case ", 0) != 0) continue;
    std::istringstream head(line.substr(5));
    std::string src, opts;
    std::size_t count;
    head >> src >> opts >> count;
    std::vector<std::string> want(count);
    for (std::size_t i = 0; i < count; i++) std::getline(in, want[i]);
    flow_db_options o;
    if (opts.find("\"inheritDir\":true") != std::string::npos) o.inherit_dir = true;
    if (opts.find("\"direction\":\"LR\"") != std::string::npos) o.direction = "LR";
    if (opts.find("\"maxEdges\":3") != std::string::npos) o.max_edges = 3;
    flow_db db(o);
    std::string got_head;
    std::vector<std::string> got;
    try {
      parse_flowchart(unhex16(src), db);
      got_head = "ok";
      got = dump(db);
    } catch (const pele_error& e) {
      got_head = "err " + hex16(to_utf16(e.what())) + " " + hex16(to_utf16(to_string(e.code())));
    }
    // The classes, tooltips and the rest follow in the oracle's dump; compare the part both sides write.
    std::string why;
    if (want.empty() || want[0] != got_head) {
      why = "result: got [" + got_head.substr(0, 60) + "] want [" + (want.empty() ? "" : want[0].substr(0, 60)) + "]";
    } else if (got_head == "ok") {
      for (std::size_t i = 0; why.empty() && i < got.size(); i++) {
        if (i + 1 >= want.size() || got[i] != want[i + 1]) why = "line " + std::to_string(i) + "\n  got:  " + got[i].substr(0, 240) + "\n  want: " + (i + 1 < want.size() ? want[i + 1].substr(0, 240) : "(none)");
      }
    }
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 4) std::cout << "differs: " << why << "\n  source: " << to_utf8(unhex16(src)).substr(0, 300) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
