#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "icons.h"
#include "label.h"

namespace pele {

// The path of an edge, and where it starts and ends and which way it heads there.
struct edge_path {
  std::string d;
  // The path as command codes, argument counts and arguments, as it is written, so that it need not be parsed again. Empty if not kept.
  std::vector<double> tokens;
  double sx = 0;
  double sy = 0;
  double sdx = 0;
  double sdy = 0;
  double ex = 0;
  double ey = 0;
  double edx = 0;
  double edy = 0;
};

// Where a self-loop's label goes, which loop_path sets.
struct loop_label {
  double label_w = 0;
  double label_h = 0;
  double label_x = 0;
  double label_y = 0;
};

// How far from each side of each node the edges that meet it start to turn: as far as the largest
// marker there needs, for every edge on the side, so that none turns sooner than its neighbour
// and crosses it.
class side_reach {
 public:
  // `side` is -1 for an end that is not on a side of a node.
  void add(int node, int side, std::string_view marker_type);
  double of(int node, int side) const;

 private:
  std::unordered_map<long long, double> reach_;
};

// Geometry and drawing of edges. Marker types are Mermaid's: arrow_point, arrow_circle,
// arrow_cross; anything else draws nothing.
class edge_geometry {
 public:
  static constexpr double kArrow = 8;
  // How far an edge runs straight before a marker at its end, where it has the room.
  static constexpr double kLead = 6;

  // Builds the path for a route of x, y, axis triples, trimming both ends to leave room for
  // markers. Between two points that are not in line, a route is one S that leaves and arrives
  // along the flow, or a step in a stepped route. Where the route itself turns a corner, a stepped
  // route rounds it a little, and any other but a linear one turns as widely as it can.
  // `start_reach` and `end_reach` say how far from its nodes the edge starts to turn, where the
  // other edges on the same side of a node need it to; by default, far enough to clear its own marker.
  static edge_path route_path(const std::vector<double>& route, std::string_view curve, double start_trim, double end_trim,
                              std::optional<double> start_reach = std::nullopt, std::optional<double> end_reach = std::nullopt);

  static double marker_trim(std::string_view type);

  // Draws a marker whose tip is at (x, y), pointing along (dx, dy).
  static std::string marker(std::string_view type, double x, double y, double dx, double dy, std::string_view color, std::string_view stroke = "none");

  // An edge label on a patch of the background, so the line does not run through the text.
  static std::string label_svg(std::string_view id, const label& l, double x, double y, const std::string& attrs, const icon_set* icons = nullptr);

  // A self-loop beside a node centred at (cx, cy) of drawn size w by h: out of the side that lies
  // across the flow and back in. `k` counts the loops already on the node, so each one reaches a
  // little further. Also places the edge's label beyond the loop.
  // A fill an edge may lie on: its bounds as left, top, right, bottom, and its style.
  struct fill {
    std::array<double, 4> box{};
    std::string shape;
  };

  // The ink of the innermost fill that holds the whole edge and its marker tips, "" for a casing
  // where it crosses a border, or nothing off the fills.
  static std::optional<std::string> over_fills(const std::string& d, const std::vector<double>& tips, const std::vector<fill>& fills, const std::string& line = "",
                                               const std::vector<double>& tokens = {});

  // A wider page-colored stroke under an edge, as faint as it; markers drop their class so marker styles skip it.
  static std::string edge_casing(const edge_path& path, std::string_view start_type, std::string_view end_type, const std::string& line = "", bool thick = false);

  // The same around markers that are already drawn: their class is dropped so marker styles skip them.
  static std::string casing(const std::string& d, const std::string& markers, const std::string& line = "", bool thick = false);

  // The innermost fill that holds the whole edge and its marker tips. `crossing` is set where the edge
  // crosses a border, and nothing is held off the fills.
  struct fill_hold {
    const fill* holder = nullptr;
    bool crossing = false;
  };
  static fill_hold fill_under(const std::string& d, const std::vector<double>& tips, const std::vector<fill>& fills, const std::vector<double>& tokens = {});

  // The fill's ink for an edge on it, or "" for a casing when unknown or when the line style sets a stroke.
  static std::string edge_ink(const fill& f, const std::string& line = "");

  // Markers with the page color inside them, with the fill's color there instead.
  static std::string hollow_on(const std::string& markers, const fill& f);

  static std::string casings_svg(const std::string& casings);

  static edge_path loop_path(double cx, double cy, double w, double h, int k, bool sideways, double base, double trim, loop_label& e);
};

}  // namespace pele
