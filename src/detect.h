#pragma once

#include <optional>
#include <string>

#include "ustring.h"

namespace pele {

enum class diagram_type {
  flowchart, sequence, class_diagram, state, er, pie, gantt, git_graph, mindmap, timeline, journey, xychart, quadrant_chart, requirement, c4, sankey, block,
  packet, kanban, architecture, radar, treemap, info, agentflow, usecase, railroad, tree_view, venn, cynefin, wardley, swimlane, ishikawa, event_modeling
};

// The name Pele uses for a diagram type: "flowchart", "class", "gitGraph", "quadrantChart" and so on.
const char* to_string(diagram_type type);

struct detected {
  diagram_type type;
  // The type as Mermaid reports it, such as `flowchart-v2` for `graph TD`.
  const char* mermaid_type;
};

// Finds the diagram type from the first words of its text, by Mermaid's detectors in the order
// Mermaid registers them. The first match wins.
class diagram_detector {
 public:
  static std::optional<detected> detect_diagram(ustring_view cleaned);
  static std::optional<diagram_type> detect(ustring_view cleaned);
};

}  // namespace pele
