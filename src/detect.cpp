#include "detect.h"

#include <functional>

#include "chars.h"

namespace pele {
namespace {

// After leading whitespace, does the text start with `word` (any case where `any_case`)?
bool starts(ustring_view s, const char* word, bool any_case = false, std::size_t* end = nullptr) {
  std::size_t i = 0;
  while (i < s.size() && is_space(s[i])) i++;
  std::size_t k = 0;
  for (; word[k]; k++) {
    if (i + k >= s.size()) return false;
    int c = s[i + k];
    if (any_case && c >= 'A' && c <= 'Z') c += 32;
    if (c != static_cast<unsigned char>(word[k])) return false;
  }
  if (end != nullptr) *end = i + k;
  return true;
}

// `word` and then a word boundary.
bool starts_bounded(ustring_view s, const char* word, bool any_case = false) {
  std::size_t end = 0;
  if (!starts(s, word, any_case, &end)) return false;
  return end >= s.size() || !is_word(s[end]);
}

// `word` and then whitespace, a colon (where `colon`), or the end.
bool starts_then(ustring_view s, const char* word, bool colon) {
  std::size_t end = 0;
  if (!starts(s, word, false, &end)) return false;
  return end >= s.size() || is_space(s[end]) || (colon && s[end] == ':');
}

struct detector {
  diagram_type type;
  const char* mermaid_type;
  std::function<bool(ustring_view)> test;
};

const std::vector<detector>& detectors() {
  static const std::vector<detector> list = {
      {diagram_type::flowchart, "flowchart-elk", [](ustring_view s) { return starts(s, "flowchart-elk"); }},
      {diagram_type::mindmap, "mindmap", [](ustring_view s) { return starts(s, "mindmap"); }},
      {diagram_type::architecture, "architecture", [](ustring_view s) { return starts(s, "architecture"); }},
      {diagram_type::agentflow, "agentflow", [](ustring_view s) { return starts_bounded(s, "agentflow-beta"); }},
      {diagram_type::c4, "c4",
       [](ustring_view s) {
         return starts(s, "C4Context") || starts(s, "C4Container") || starts(s, "C4Component") || starts(s, "C4Dynamic") || starts(s, "C4Deployment");
       }},
      {diagram_type::kanban, "kanban", [](ustring_view s) { return starts(s, "kanban"); }},
      {diagram_type::class_diagram, "classDiagram", [](ustring_view s) { return starts(s, "classDiagram"); }},
      {diagram_type::er, "er", [](ustring_view s) { return starts(s, "erDiagram"); }},
      {diagram_type::gantt, "gantt", [](ustring_view s) { return starts(s, "gantt"); }},
      {diagram_type::info, "info", [](ustring_view s) { return starts(s, "info"); }},
      {diagram_type::pie, "pie", [](ustring_view s) { return starts(s, "pie"); }},
      {diagram_type::requirement, "requirement", [](ustring_view s) { return starts(s, "requirement"); }},
      {diagram_type::sequence, "sequence", [](ustring_view s) { return starts(s, "sequenceDiagram"); }},
      {diagram_type::swimlane, "swimlane", [](ustring_view s) { return starts_bounded(s, "swimlane-beta"); }},
      {diagram_type::flowchart, "flowchart-v2", [](ustring_view s) { return starts(s, "graph") || starts(s, "flowchart"); }},
      {diagram_type::timeline, "timeline", [](ustring_view s) { return starts(s, "timeline"); }},
      {diagram_type::git_graph, "gitGraph", [](ustring_view s) { return starts(s, "gitGraph"); }},
      {diagram_type::state, "stateDiagram", [](ustring_view s) { return starts(s, "stateDiagram"); }},
      {diagram_type::journey, "journey", [](ustring_view s) { return starts(s, "journey"); }},
      {diagram_type::quadrant_chart, "quadrantChart", [](ustring_view s) { return starts(s, "quadrantChart"); }},
      {diagram_type::sankey, "sankey", [](ustring_view s) { return starts(s, "sankey"); }},
      {diagram_type::packet, "packet", [](ustring_view s) { return starts(s, "packet"); }},
      {diagram_type::xychart, "xychart", [](ustring_view s) { return starts(s, "xychart"); }},
      {diagram_type::block, "block", [](ustring_view s) { return starts(s, "block"); }},
      {diagram_type::event_modeling, "eventmodeling", [](ustring_view s) { return starts(s, "eventmodeling"); }},
      {diagram_type::tree_view, "treeView", [](ustring_view s) { return starts(s, "treeView-beta"); }},
      {diagram_type::radar, "radar", [](ustring_view s) { return starts(s, "radar-beta"); }},
      {diagram_type::ishikawa, "ishikawa", [](ustring_view s) { return starts_bounded(s, "ishikawa", true); }},
      {diagram_type::treemap, "treemap", [](ustring_view s) { return starts(s, "treemap"); }},
      {diagram_type::railroad, "railroad", [](ustring_view s) { return starts(s, "railroad-beta", true); }},
      {diagram_type::railroad, "railroadEbnf", [](ustring_view s) { return starts(s, "railroad-ebnf-beta", true); }},
      {diagram_type::railroad, "railroadAbnf", [](ustring_view s) { return starts(s, "railroad-abnf-beta", true); }},
      {diagram_type::railroad, "railroadPeg", [](ustring_view s) { return starts(s, "railroad-peg-beta", true); }},
      {diagram_type::venn, "venn", [](ustring_view s) { return starts(s, "venn-beta"); }},
      {diagram_type::wardley, "wardley", [](ustring_view s) { return starts(s, "wardley-beta", true); }},
      {diagram_type::cynefin, "cynefin", [](ustring_view s) { return starts_then(s, "cynefin-beta", true); }},
      {diagram_type::usecase, "usecase", [](ustring_view s) { return starts_then(s, "usecase-beta", false); }},
  };
  return list;
}

}  // namespace

const char* to_string(diagram_type type) {
  switch (type) {
    case diagram_type::flowchart: return "flowchart";
    case diagram_type::sequence: return "sequence";
    case diagram_type::class_diagram: return "class";
    case diagram_type::state: return "state";
    case diagram_type::er: return "er";
    case diagram_type::pie: return "pie";
    case diagram_type::gantt: return "gantt";
    case diagram_type::git_graph: return "gitGraph";
    case diagram_type::mindmap: return "mindmap";
    case diagram_type::timeline: return "timeline";
    case diagram_type::journey: return "journey";
    case diagram_type::xychart: return "xychart";
    case diagram_type::quadrant_chart: return "quadrantChart";
    case diagram_type::requirement: return "requirement";
    case diagram_type::c4: return "c4";
    case diagram_type::sankey: return "sankey";
    case diagram_type::block: return "block";
    case diagram_type::packet: return "packet";
    case diagram_type::kanban: return "kanban";
    case diagram_type::architecture: return "architecture";
    case diagram_type::radar: return "radar";
    case diagram_type::treemap: return "treemap";
    case diagram_type::info: return "info";
    case diagram_type::agentflow: return "agentflow";
    case diagram_type::usecase: return "usecase";
    case diagram_type::railroad: return "railroad";
    case diagram_type::tree_view: return "treeView";
    case diagram_type::venn: return "venn";
    case diagram_type::cynefin: return "cynefin";
    case diagram_type::wardley: return "wardley";
    case diagram_type::swimlane: return "swimlane";
    case diagram_type::ishikawa: return "ishikawa";
    default: return "eventmodeling";
  }
}

std::optional<detected> diagram_detector::detect_diagram(ustring_view cleaned) {
  for (const detector& d : detectors())
    if (d.test(cleaned)) return detected{d.type, d.mermaid_type};
  return std::nullopt;
}

std::optional<diagram_type> diagram_detector::detect(ustring_view cleaned) {
  const auto found = detect_diagram(cleaned);
  if (!found) return std::nullopt;
  return found->type;
}

}  // namespace pele
