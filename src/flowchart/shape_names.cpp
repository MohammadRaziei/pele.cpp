#include "shape_names.h"

#include <unordered_map>

namespace pele::flowchart {
namespace {

// Generated from Pele's src/diagrams/flowchart/shapes.ts by tools/gen_shape_names.py.
const char* const kShapes[][8] = {
    {"rect", "proc", "process", "rectangle", "squareRect", nullptr},
    {"rounded", "event", "roundedRect", nullptr},
    {"stadium", "terminal", "pill", nullptr},
    {"fr-rect", "subprocess", "subproc", "framed-rectangle", "subroutine", nullptr},
    {"cyl", "db", "database", "cylinder", nullptr},
    {"datastore", "data-store", nullptr},
    {"folder", "directory", nullptr},
    {"bucket", nullptr},
    {"console", nullptr},
    {"browser", nullptr},
    {"person", nullptr},
    {"circle", "circ", nullptr},
    {"bang", nullptr},
    {"cloud", nullptr},
    {"diam", "decision", "diamond", "question", nullptr},
    {"hex", "hexagon", "prepare", nullptr},
    {"lean-r", "lean-right", "in-out", "lean_right", nullptr},
    {"lean-l", "lean-left", "out-in", "lean_left", nullptr},
    {"trap-b", "priority", "trapezoid-bottom", "trapezoid", nullptr},
    {"trap-t", "manual", "trapezoid-top", "inv-trapezoid", "inv_trapezoid", nullptr},
    {"dbl-circ", "double-circle", "doublecircle", nullptr},
    {"text", nullptr},
    {"notch-rect", "card", "notched-rectangle", nullptr},
    {"lin-rect", "lined-rectangle", "lined-process", "lin-proc", "shaded-process", nullptr},
    {"sm-circ", "start", "small-circle", "stateStart", nullptr},
    {"fr-circ", "stop", "framed-circle", "stateEnd", nullptr},
    {"fork", "join", "forkJoin", nullptr},
    {"hourglass", "collate", nullptr},
    {"brace", "comment", "brace-l", nullptr},
    {"brace-r", nullptr},
    {"braces", nullptr},
    {"bolt", "com-link", "lightning-bolt", nullptr},
    {"doc", "document", nullptr},
    {"delay", "half-rounded-rectangle", nullptr},
    {"h-cyl", "das", "horizontal-cylinder", nullptr},
    {"lin-cyl", "disk", "lined-cylinder", nullptr},
    {"curv-trap", "curved-trapezoid", "display", nullptr},
    {"div-rect", "div-proc", "divided-rectangle", "divided-process", nullptr},
    {"tri", "extract", "triangle", nullptr},
    {"win-pane", "internal-storage", "window-pane", nullptr},
    {"f-circ", "junction", "filled-circle", nullptr},
    {"notch-pent", "loop-limit", "notched-pentagon", nullptr},
    {"flip-tri", "manual-file", "flipped-triangle", nullptr},
    {"sl-rect", "manual-input", "sloped-rectangle", nullptr},
    {"docs", "documents", "st-doc", "stacked-document", nullptr},
    {"st-rect", "procs", "processes", "stacked-rectangle", nullptr},
    {"bow-rect", "stored-data", "bow-tie-rectangle", nullptr},
    {"cross-circ", "summary", "crossed-circle", nullptr},
    {"tag-doc", "tagged-document", nullptr},
    {"tag-rect", "tagged-rectangle", "tag-proc", "tagged-process", nullptr},
    {"flag", "paper-tape", nullptr},
    {"odd", "rect_left_inv_arrow", nullptr},
    {"lin-doc", "lined-document", nullptr},
};

const char* const kInternal[] = {
    "state",
    "choice",
    "note",
    "composite",
    "rectWithTitle",
    "labelRect",
    "block_arrow",
    "collapsedGroup",
    "iconSquare",
    "iconCircle",
    "icon",
    "iconRounded",
    "imageSquare",
    "anchor",
    "kanbanItem",
    "mindmapCircle",
    "defaultMindmapNode",
    "classBox",
    "erBox",
    "requirementBox",
    "usecaseActor",
    "usecaseActorHollow",
    "usecaseActorAwesome",
    "usecaseActorIcon",
    "usecaseBusiness",
    "usecaseEllipse",
    "usecaseJsonTable",
};

const std::unordered_map<std::string, std::string>& syntax() {
  static const std::unordered_map<std::string, std::string> table = {
      {"square", "rect"},
      {"round", "rounded"},
      {"ellipse", "ellipse"},
      {"hexagon", "hex"},
      {"diamond", "diam"},
  };
  return table;
}

const std::unordered_map<std::string, std::string>& canonical() {
  static const std::unordered_map<std::string, std::string> table = [] {
    std::unordered_map<std::string, std::string> t;
    for (const auto& names : kShapes)
      for (const char* const* n = names; *n != nullptr; n++) t[*n] = names[0];
    for (const char* n : kInternal) t[n] = n;
    return t;
  }();
  return table;
}

}  // namespace

bool is_valid_shape(const std::string& name) { return canonical().count(name) > 0; }

std::string canonical_shape(const std::optional<std::string>& name) {
  if (!name) return "rect";
  const auto s = syntax().find(*name);
  if (s != syntax().end()) return s->second;
  const auto c = canonical().find(*name);
  return c != canonical().end() ? c->second : "rect";
}

}  // namespace pele::flowchart
