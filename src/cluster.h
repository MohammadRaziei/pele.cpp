#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "compound.h"
#include "icons.h"
#include "label.h"

namespace pele {

// The x of every route point, by its rounded y: the edges that cross a cluster's top border.
class crossing_map {
 public:
  void mark(const std::vector<double>& route);

  // Picks where a cluster title sits along the top edge so that edges entering there do not cross it.
  double title_x(const compound_node& c, double width, double pad) const;

  // Whether an edge that enters at the top runs through a title placed at `x`.
  bool crossed(const compound_node& c, double width, double x) const;

 private:
  std::unordered_map<long long, std::vector<double>> crossings_;
};

// A title that an edge runs through, to be drawn over the edges: the text, on an outline in the
// colour of the cluster behind it, so that only the line is seen to stop short of the letters.
// `nodes` and `index` find how many clusters deep the title is, each of which tints the background.
std::string struck_title(const label& l, double x, double y, const std::string& attrs, const std::string& id, const std::vector<compound_node>& nodes,
                         std::size_t index, const icon_set* icons = nullptr);

}  // namespace pele
