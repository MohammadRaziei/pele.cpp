#include "mindmap/mindmap.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "common/icon.h"
#include "common/outline.h"
#include "errors.h"
#include "fit_width.h"
#include "js_math.h"
#include "label.h"
#include "shapes.h"
#include "svg.h"
#include "svg_document.h"
#include "text_measurer.h"
#include "theme.h"

namespace pele::mindmap {
namespace {

constexpr int kSections = 11;

class mindmap_db final : public outline::outline_db {
 public:
  mindmap_model model;

  void add_node(int level, const ustring& id, const ustring& descr, int type, const std::optional<ustring>& /*shape_data*/) override {
    auto& nodes = model.nodes;
    const bool is_root = nodes.empty();
    if (is_root) base_level_ = level;
    level -= base_level_;

    // Mermaid's parent is the last node with a smaller indentation, wherever it is in the tree.
    std::size_t depth = open_.size();
    while (depth > 0 && nodes[static_cast<std::size_t>(open_[depth - 1])].level >= level) depth--;
    const int parent = depth > 0 ? open_[depth - 1] : -1;
    if (parent < 0 && !is_root) {
      error_info info;
      info.type = "mindmap";
      throw pele_error("There can be only one root. No parent could be found for (\"" + to_utf8(descr) + "\")", error_code::semantic, std::move(info));
    }
    mindmap_node node;
    node.id = static_cast<int>(nodes.size());
    node.node_id = id;
    node.level = level;
    node.descr = descr;
    node.type = type;
    node.is_root = is_root;
    if (parent >= 0) {
      mindmap_node& p = nodes[static_cast<std::size_t>(parent)];
      node.section = p.is_root ? static_cast<int>(p.children.size()) % kSections : p.section;
      p.children.push_back(node.id);
    }
    open_.resize(depth);
    open_.push_back(node.id);
    nodes.push_back(std::move(node));
  }

  // Mermaid throws a TypeError when a decoration comes before any node. Here it is ignored.
  void decorate_node(const std::optional<ustring>& icon, const std::optional<ustring>& css_class) override {
    if (model.nodes.empty()) return;
    mindmap_node& node = model.nodes.back();
    if (icon && !icon->empty()) node.icon = *icon;
    if (css_class && !css_class->empty()) node.css_class = *css_class;
  }

 private:
  int base_level_ = 0;
  // The chain of nodes that can still take children, with strictly increasing levels.
  std::vector<int> open_;
};

// Indexed by Mermaid's node type. A node without delimiters is text on a line, except the root.
const char* const kShapes[] = {"", "rounded", "rect", "circle", "cloud", "bang", "hex"};
const char* const kRootShape = "stadium";

constexpr double kGapX = 36;
constexpr double kGapY = 8;
// Between two subtrees, of which at least one has children of its own.
constexpr double kGroupGap = 18;
constexpr double kBranchGapX = 52;
constexpr double kBranchGapY = 28;
constexpr double kTextPad = 6;
constexpr double kPillPad = 12;
constexpr double kTint = 0.22;
constexpr double kIconGap = 6;
// Running down, each level is set in by this much, and links leave a node this far from its left edge.
constexpr double kIndent = 28;
constexpr double kStem = 12;
constexpr double kBend = 8;
constexpr double kRowGap = 10;

// A label with each line starting at `left`, for an outline, where centered lines would look ragged.
std::string left_label(const label& l, double left, double cy, const icon_set& icons) {
  if (l.lines.size() < 2) return svg::label_svg(l, left + l.width / 2, cy, " class=\"pele-label\"", &icons);
  std::string out;
  const double top = cy - l.height / 2;
  for (std::size_t k = 0; k < l.lines.size(); k++)
    out += svg::label_svg(label_line(l, k), left + l.widths[k] / 2, top + line_middle(l, k), " class=\"pele-label\"", &icons);
  return out;
}

// `down` draws the map as an outline: the root at the top, and each level set in under its parent.
// It is far narrower than branches spreading to both sides, and as tall as it needs to be.
rendered draw(const mindmap_model& model, const config& settings, const render_options& options, bool down) {
  const double size = options.font_size ? *options.font_size : 16;
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options.measurer != nullptr ? *options.measurer : default_measurer;
  const double pad = options.padding ? *options.padding : 8;
  const icon_set icons(options.icons);
  const label_layouter layouter(measurer, &icons);
  const shape_registry& shapes_by_name = shape_registry::instance();

  const yaml_value* conf = settings.find("mindmap");
  if (conf != nullptr && !conf->is_object()) conf = nullptr;
  const yaml_value* max_node_width = conf != nullptr ? conf->find("maxNodeWidth") : nullptr;
  const double wrap = max_node_width != nullptr && max_node_width->is_number() && max_node_width->as_number() > 0 ? std::min(max_node_width->as_number(), 2000.0) : 200;
  const double glyph = size * 1.25;

  const auto& nodes = model.nodes;
  const std::size_t n = nodes.size();
  std::vector<int> parent(n, 0);
  std::vector<int> depth(n, 0);
  for (std::size_t i = 0; i < n; i++) {
    for (int child : nodes[i].children) {
      parent[static_cast<std::size_t>(child)] = static_cast<int>(i);
      depth[static_cast<std::size_t>(child)] = depth[i] + 1;
    }
  }
  // An outline has the width the host gives it, less what its level is set in.
  const double room = down && options.max_width && *options.max_width > 0 ? *options.max_width - 2 * pad : std::numeric_limits<double>::infinity();
  std::vector<label> labels(n);
  std::vector<std::string> shapes(n);
  std::vector<double> w(n, 0), h(n, 0), dy(n, 0);
  // Where links meet the node, measured down from its top: its middle.
  std::vector<double> anchor(n, 0);
  // Whether the host has the node's icon. One it does not have takes no room.
  std::vector<char> drawn(n, 0);

  for (std::size_t i = 0; i < n; i++) {
    const mindmap_node& node = nodes[i];
    drawn[i] = node.icon && !icons.markup(to_utf8(*node.icon)).empty() ? 1 : 0;
    const double fit = std::min(wrap, std::max(72.0, room - depth[i] * kIndent - 2 * kTextPad - (drawn[i] ? glyph + kIconGap : 0)));
    labels[i] = layouter.layout(node.descr, true, size, fit);
    const label& lab = labels[i];
    const double tw = lab.width + (drawn[i] ? glyph + (lab.width > 0 ? kIconGap : 0) : 0);
    const double th = std::max(lab.height, js_round(size * 1.5));
    std::string shape = node.type >= 0 && node.type <= 6 ? kShapes[node.type] : "";
    if (shape.empty() && i == 0) shape = kRootShape;
    // In an outline the root heads a column, and the trunk drops from under its left end. A circle
    // or a cloud would stand wide of that, so the root is a pill there unless it is a box already.
    if (down && i == 0 && shape != "rect" && shape != "rounded") shape = kRootShape;
    shapes[i] = shape;
    if (shape.empty()) {
      // A node with no shape of its own is plain text when it ends a branch, and text in a
      // tinted pill when the branch goes on from it, which sets the levels apart.
      const bool pill = !node.children.empty();
      w[i] = tw + 2 * (pill ? kPillPad : kTextPad);
      h[i] = th + (pill ? 6 : 0);
      anchor[i] = h[i] / 2;
    } else {
      const shape_size s = shapes_by_name.size(shape, tw, th);
      w[i] = s.w;
      h[i] = s.h;
      dy[i] = s.dy;
      anchor[i] = s.h / 2;
    }
  }

  // Each subtree takes a band of height `extent`. `rel` is a subtree's offset among its siblings,
  // `own` the node's offset inside its band, and `kids` the offset of its children's bands.
  std::vector<double> extent(n, 0), rel(n, 0), own(n, 0), kids(n, 0);
  double stacked = 0;

  // Stacks subtrees top to bottom and returns the height at which their parent's anchor belongs.
  const auto stack = [&](const std::vector<int>& list, std::size_t from, std::size_t to, double gap) -> double {
    double y = 0;
    double last = 0;
    for (std::size_t k = from; k < to; k++) {
      const std::size_t c = static_cast<std::size_t>(list[k]);
      rel[c] = y;
      const bool grouped = !nodes[c].children.empty() || (k + 1 < to && !nodes[static_cast<std::size_t>(list[k + 1])].children.empty());
      last = grouped ? std::max(gap, kGroupGap) : gap;
      y += extent[c] + last;
    }
    stacked = y - last;
    const std::size_t a = static_cast<std::size_t>(list[from]);
    const std::size_t b = static_cast<std::size_t>(list[to - 1]);
    return (rel[a] + own[a] + anchor[a] + rel[b] + own[b] + anchor[b]) / 2;
  };

  // Nodes are in depth-first order, so going backwards visits children before their parent.
  for (std::size_t i = n; i-- > 1;) {
    const auto& children = nodes[i].children;
    if (children.empty()) {
      extent[i] = h[i];
      continue;
    }
    const double top = stack(children, 0, children.size(), kGapY) - anchor[i];
    const double low = std::min(0.0, top);
    extent[i] = std::max(stacked, top + h[i]) - low;
    own[i] = top - low;
    kids[i] = -low;
  }

  std::vector<double> x(n, 0), y(n, 0), band(n, 0);
  std::vector<int> side(n, 0);
  // Running down: how far the trunk under each node has been drawn.
  std::vector<double> trunk(n, 0);

  if (down) {
    // Nodes are in depth-first order, which is the order of an outline's rows.
    double row = 0;
    for (std::size_t i = 0; i < n; i++) {
      if (i > 0 && parent[i] == 0) row += kRowGap;
      x[i] = depth[i] * kIndent + w[i] / 2;
      y[i] = row;
      row += h[i] + kRowGap;
    }
  } else if (n > 0) {
    // The first branches go right and the rest left, split where the two sides are closest in height.
    const auto& branches = nodes[0].children;
    const std::size_t count = branches.size();
    double total = 0;
    for (int branch : branches) total += extent[static_cast<std::size_t>(branch)];
    std::size_t split = count;
    double best = std::numeric_limits<double>::infinity();
    double right = 0;
    for (std::size_t k = 1; k < count; k++) {
      right += extent[static_cast<std::size_t>(branches[k - 1])];
      const double diff = std::abs(2 * right - total + (2.0 * static_cast<double>(k) - static_cast<double>(count)) * kBranchGapY);
      if (diff < best) {
        best = diff;
        split = k;
      }
    }
    for (int pass = 0; pass < 2; pass++) {
      const std::size_t from = pass == 0 ? 0 : split;
      const std::size_t to = pass == 0 ? split : count;
      const int dir = pass == 0 ? 1 : -1;
      if (from >= count) break;
      const double middle = stack(branches, from, to, kBranchGapY);
      for (std::size_t k = from; k < to; k++) {
        const std::size_t c = static_cast<std::size_t>(branches[k]);
        band[c] = rel[c] - middle;
        side[c] = dir;
      }
    }
    y[0] = -h[0] / 2;
    for (std::size_t i = 0; i < n; i++) {
      if (i > 0) y[i] = band[i] + own[i];
      for (int child : nodes[i].children) {
        const std::size_t c = static_cast<std::size_t>(child);
        if (i > 0) {
          side[c] = side[i];
          band[c] = band[i] + kids[i] + rel[c];
        }
        x[c] = x[i] + side[c] * (w[i] / 2 + (i > 0 ? kGapX : kBranchGapX) + w[c] / 2);
      }
    }
  }

  double min_x = 0, max_x = 0, min_y = 0, max_y = 0;
  std::string out;
  std::string edges;
  std::string body;
  std::string color;
  const auto flush = [&](const std::optional<int>& section) {
    if (body.empty()) return;
    out += "<g class=\"pele-branch pele-section-" + std::to_string(section.value_or(0)) + "\">" + "<g class=\"pele-edges\" fill=\"none\" stroke=\"" + color +
           "\" stroke-width=\"1.5\">" + edges + "</g>" + body + "</g>";
    edges.clear();
    body.clear();
  };

  // The root is drawn last, on top of the links that leave it.
  for (std::size_t k = 1; k <= n; k++) {
    const std::size_t i = k % n;
    const mindmap_node& node = nodes[i];
    const std::string& shape = shapes[i];
    const label& lab = labels[i];
    const double cx = x[i];
    const double top = y[i];
    min_x = std::min(min_x, cx - w[i] / 2);
    max_x = std::max(max_x, cx + w[i] / 2);
    min_y = std::min(min_y, top);
    max_y = std::max(max_y, top + h[i]);

    if (i == 0 || parent[i] == 0) flush(nodes[k - 1].section);
    if (i > 0 && down) {
      if (parent[i] == 0) color = theme::series_color(node.section.value_or(0));
      const std::size_t p = static_cast<std::size_t>(parent[i]);
      // The link drops from under its parent and turns in to the node's left. It starts where
      // the link to the sibling above turned in, so each stretch of a shared trunk has one color.
      // Under the root's rounded end, the trunk starts where that end has come down to the bottom.
      const double stem = p == 0 ? std::min(std::max(kStem, h[0] / 2), kIndent - kBend) : kStem;
      const double x1 = x[p] - w[p] / 2 + std::min(stem, w[p] / 2);
      const double y1 = trunk[p] != 0 && !std::isnan(trunk[p]) ? trunk[p] : y[p] + h[p];
      const double left = cx - w[i] / 2;
      const double x2 = left + (!shape.empty() ? shapes_by_name.inset(shape, w[i], h[i], 3) : 0);
      const double y2 = top + anchor[i];
      const double bend = std::max(0.0, std::min({kBend, x2 - x1, y2 - y1}));
      // The next link carries on from where this one leaves the trunk, so the trunk has no gap.
      trunk[p] = y2 - bend;
      edges += "<path class=\"pele-edge\" d=\"M" + svg::num(x1) + "," + svg::num(y1) + "V" + svg::num(y2 - bend) + "Q" + svg::num(x1) + "," + svg::num(y2) + " " + svg::num(x1 + bend) +
               "," + svg::num(y2) + "H" + svg::num(x2) + "\"/>";
    } else if (i > 0) {
      if (parent[i] == 0) color = theme::series_color(node.section.value_or(0));
      const std::size_t p = static_cast<std::size_t>(parent[i]);
      const int dir = side[i];
      const int from = dir > 0 ? 1 : 3;
      const double x1 = x[p] + dir * (w[p] / 2 - (!shapes[p].empty() ? shapes_by_name.inset(shapes[p], w[p], h[p], from) : 0));
      const double y1 = y[p] + anchor[p];
      const double near = cx - (dir * w[i]) / 2;
      const double x2 = near + (!shape.empty() ? dir * shapes_by_name.inset(shape, w[i], h[i], 4 - from) : 0);
      const double y2 = top + anchor[i];
      const std::string mid = svg::num((x1 + x2) / 2);
      edges += "<path class=\"pele-edge\" d=\"M" + svg::num(x1) + "," + svg::num(y1) + "C" + mid + "," + svg::num(y1) + " " + mid + "," + svg::num(y2) + " " + svg::num(x2) + "," + svg::num(y2) + "\"/>";
    }

    const bool pill = shape.empty() && !node.children.empty();
    // Every node off the root is filled with a tint of its branch's color and outlined in it,
    // whether its shape was given in the source or is the pill of a node that branches.
    // The root is neutral.
    const std::string tint = " fill=\"" + color + "\" fill-opacity=\"" + js_number_string(kTint) + "\" stroke=\"" + color + "\"";
    std::string inner;
    if (!shape.empty()) {
      inner = i > 0 ? shapes_by_name.draw(shape, w[i], h[i], tint, " stroke=\"" + color + "\"")
                    : shapes_by_name.draw(shape, w[i], h[i], " fill=\"var(--_s)\" stroke=\"var(--_b)\"", " stroke=\"var(--_b)\"");
    } else if (pill) {
      inner = "<rect x=\"" + svg::num(-w[i] / 2) + "\" y=\"" + svg::num(-h[i] / 2) + "\" width=\"" + svg::num(w[i]) + "\" height=\"" + svg::num(h[i]) + "\" rx=\"" + svg::num(h[i] / 2) + "\"" + tint + "/>";
    }
    double shift = 0;
    if (node.icon && !node.icon->empty() && drawn[i]) {
      shift = (glyph + (lab.width > 0 ? kIconGap : 0)) / 2;
      inner += icon_svg(*node.icon, -shift - lab.width / 2, dy[i] - glyph / 2, glyph, icons);
    }
    const std::string kind = !shape.empty() ? shape : (pill ? "branch" : "text");
    const std::string group = "<g class=\"pele-node pele-shape-" + kind + (i > 0 ? "" : " pele-root") +
                              theme::class_names(node.css_class ? ustring_view(*node.css_class) : ustring_view()) + "\" data-id=\"" + svg::esc_text(node.node_id) + "\" transform=\"translate(" +
                              svg::num(cx) + "," + svg::num(top + h[i] / 2) + ")\">" + inner +
                              (down && shape.empty() ? left_label(lab, shift - lab.width / 2, dy[i], icons) : svg::label_svg(lab, shift, dy[i], " class=\"pele-label\"", &icons)) + "</g>";
    if (i > 0) body += group;
    else out += group;
  }

  const label title = layouter.layout(model.title ? ustring_view(*model.title) : ustring_view(), false, size, fit_width::title_room(options), text_style::bold);
  const double title_height = title.height > 0 ? title.height + 16 : 0;
  const double width = std::max(max_x - min_x, title.width);
  const double total_width = std::ceil(width + 2 * pad);
  const double total_height = std::ceil(max_y - min_y + title_height + 2 * pad);
  const std::string content = svg::label_svg(title, total_width / 2, pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") + "<g transform=\"translate(" +
                              svg::num(pad - min_x + (width - max_x + min_x) / 2) + "," + svg::num(pad + title_height - min_y) + ")\">" + out + "</g>";

  rendered result;
  result.svg = svg_document::build("mindmap", total_width, total_height, size, options, accessible{}, content);
  result.width = total_width;
  result.height = total_height;
  return result;
}

}  // namespace

mindmap_model parse_mindmap(const ustring& source, const std::optional<ustring>& title) {
  mindmap_db db;
  db.model.title = title;
  outline::parse_outline(source, db, false);
  return std::move(db.model);
}

rendered render_mindmap(const mindmap_model& model, const config& settings, const render_options& options) {
  return fit_width::turn_to_fit(options, [&](bool down) { return draw(model, settings, options, down); });
}

}  // namespace pele::mindmap
