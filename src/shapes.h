#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "compound.h"

namespace pele {

struct shape_size {
  double w = 0;
  double h = 0;
  // Vertical shift of the label from the shape's centre.
  double dy = 0;
};

inline constexpr double kPadX = 18;
inline constexpr double kPadY = 12;

// A node shape: how big it is for a label of a given size, how it is drawn, and where its outline is.
class shape {
 public:
  virtual ~shape() = default;

  virtual shape_size size(double tw, double th) const = 0;
  // `a` is the attributes of the shape's body (fill, stroke) and `line` those of its lines.
  virtual std::string draw(double w, double h, const std::string& a, const std::string& line) const = 0;
  // Distance from the bounding box to the outline on a side: 0 top, 1 right, 2 bottom, 3 left.
  // `t` is how far along the side from its middle, to the right or down.
  virtual double inset(double w, double h, int side, double t) const = 0;
  // How much of a side edges may spread along, centred on its middle: the top and bottom, or the
  // left and right when `sideways`. A shape without one keeps its edges at the middle.
  virtual double span(double w, double h, bool sideways) const = 0;
  virtual bool has_label() const = 0;
};

// The shapes by their Mermaid names. A name that is not one draws as a rectangle.
class shape_registry {
 public:
  static const shape_registry& instance();

  // The shape of that name, or null.
  const shape* find(std::string_view name) const;
  // The shape of that name, or the rectangle.
  const shape& get(std::string_view name) const;

  shape_size size(std::string_view name, double tw, double th) const { return get(name).size(tw, th); }
  std::string draw(std::string_view name, double w, double h, const std::string& a, const std::string& line) const { return get(name).draw(w, h, a, line); }
  double inset(std::string_view name, double w, double h, int side, double t = 0) const;
  double span(std::string_view name, double w, double h, bool sideways) const;
  bool has_label(std::string_view name) const;

 private:
  shape_registry();

  std::unordered_map<std::string, std::unique_ptr<shape>> shapes_;
  const shape* fallback_ = nullptr;
};

// The shape a node is drawn as, and how big.
struct shape_view {
  std::string shape;
  double w = 0;
  double h = 0;
};

// The side of a node's layout box that a route end lies on: 0 top, 1 right, 2 bottom, 3 left, or -1 for none.
int end_side(const std::vector<double>& route, std::size_t at, const compound_node& c);

// Moves a route end from a node's layout box onto the outline of the shape drawn in it.
void inset_route(std::vector<double>& route, std::size_t at, const shape_view& view, const compound_node& c);

}  // namespace pele
