#include "render.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "chars.h"
#include "cluster.h"
#include "compound.h"
#include "edges.h"
#include "entities.h"
#include "fit_width.h"
#include "graph.h"
#include "js_math.h"
#include "label.h"
#include "shape_names.h"
#include "shapes.h"
#include "svg.h"
#include "svg_document.h"
#include "theme.h"
#include "url.h"

namespace pele::flowchart {
namespace {

constexpr double kNodeSep = 40;
constexpr double kEdgeSep = 16;
constexpr double kRankSep = 48;
constexpr double kGroupPad = 20;
constexpr double kLoop = 26;
const char* const kShapeAttrs = " fill=\"var(--_s)\" stroke=\"var(--_b)\"";
const char* const kLineAttrs = " stroke=\"var(--_b)\"";

constexpr double kInf = std::numeric_limits<double>::infinity();

// A positive number set in the config, up to 2000, or the fallback.
double number_option(const yaml_value* section, const char* key, double fallback) {
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* value = section->find(key);
  return value != nullptr && value->is_number() && value->as_number() > 0 ? std::min(value->as_number(), 2000.0) : fallback;
}

double asset_size(const std::optional<double>& value, double fallback) { return value && *value > 0 && *value <= 4000 ? *value : fallback; }

ustring strip_default(const ustring& classes) {
  // /^default\s?/
  if (classes.compare(0, 7, u"default") != 0) return classes;
  std::size_t k = 7;
  if (k < classes.size() && is_space(classes[k])) k++;
  return classes.substr(k);
}

// Removes the classes Pele sets on every edge.
ustring strip_edge_classes(const ustring& classes) {
  static const char16_t* const words[] = {u"edge-thickness-normal", u"edge-pattern-solid", u"flowchart-link"};
  ustring out;
  for (std::size_t i = 0; i < classes.size();) {
    bool matched = false;
    for (const char16_t* w : words) {
      const ustring word(w);
      if (classes.compare(i, word.size(), word) == 0) {
        i += word.size();
        matched = true;
        break;
      }
    }
    if (!matched) out += classes[i++];
  }
  return out;
}

bool has_internal_link(const std::string& classes) {
  const std::string padded = classes + " ";
  const std::string needle = " internal-link";
  for (std::size_t at = padded.find(needle); at != std::string::npos; at = padded.find(needle, at + 1)) {
    const std::size_t end = at + needle.size();
    if (end >= padded.size() || padded[end] == ' ') return true;
  }
  return false;
}

// What the renderer knows about one node once it has measured it.
struct node_view {
  const graph_node* node = nullptr;
  theme::resolved_style style;
  label text;
  std::string shape;
  double w = 0;
  double h = 0;
  double dy = 0;
  int loops = 0;
};

class flowchart_renderer {
 public:
  flowchart_renderer(const flow_db& db, const config& settings, const render_options& options) : db_(db), settings_(settings), options_(options), icons_(options.icons) {}

  rendered render() {
    auto fitted = [&](bool down) -> rendered { return fit_width::tighten(options_, [&](bool tight) { return draw(down, tight); }); };
    return fit_width::runs_across(db_.direction) ? fit_width::turn_to_fit(options_, fitted) : fitted(false);
  }

 private:
  struct frame {
    flow_graph graph;
    std::vector<node_view> views;
    std::vector<compound_node> cnodes;
    std::vector<compound_edge> cedges;
    std::vector<label> edge_labels;
    std::vector<const graph_edge*> drawn;
    flow_dir root_dir = flow_dir::tb;
    double size = 16;
    double edge_size = 14;
    double pad = 8;
    double group_pad = kGroupPad;
    double wrap_width = 200;
    const yaml_value* flow = nullptr;
    std::vector<rendered_link> links;
    std::string curve_name;
  };

  const yaml_value* flow_section() const {
    const yaml_value* f = settings_.find("flowchart");
    return f != nullptr && f->is_object() ? f : nullptr;
  }

  bool flows_sideways(const frame& f, std::size_t i) const {
    std::optional<flow_dir> dir;
    long hops = 0;
    for (int p = f.cnodes[i].parent; p >= 0 && !dir && hops < 10000; p = f.cnodes[p].parent, hops++)
      if (f.cnodes[p].has_dir) dir = f.cnodes[p].dir;
    const flow_dir d = dir ? *dir : f.root_dir;
    return d == flow_dir::lr || d == flow_dir::rl;
  }

  rendered draw(bool turned, bool tight) {
    frame f;
    f.group_pad = tight ? 12 : kGroupPad;
    f.flow = flow_section();
    std::optional<std::string> default_curve;
    if (f.flow != nullptr) {
      const yaml_value* curve = f.flow->find("curve");
      if (curve != nullptr && curve->is_string()) default_curve = to_utf8(curve->as_string());
    }
    f.graph = build_flow_graph(db_, default_curve);
    f.size = options_.font_size ? *options_.font_size : 16;
    f.edge_size = js_round(f.size * 0.875);
    const metrics_measurer default_measurer;
    const text_measurer& measurer = options_.measurer != nullptr ? *options_.measurer : default_measurer;
    const label_layouter layouter(measurer, &icons_);
    f.wrap_width = number_option(f.flow, "wrappingWidth", 200);
    f.pad = options_.padding ? *options_.padding : 8;

    build_views(f, layouter);
    f.root_dir = turned ? flow_dir::tb : parse_direction(db_.direction ? *db_.direction : std::string());
    set_spans(f);
    build_edges(f, layouter);

    layered_options layout_options;
    layout_options.node_sep = number_option(f.flow, "nodeSpacing", kNodeSep) * (tight ? 0.5 : 1);
    layout_options.edge_sep = kEdgeSep;
    layout_options.rank_sep = number_option(f.flow, "rankSpacing", kRankSep) * (tight ? 0.75 : 1);
    layout_options.port_sep = 20;
    const yaml_value* curve = f.flow != nullptr ? f.flow->find("curve") : nullptr;
    const bool curve_is_string = curve != nullptr && curve->is_string();
    layout_options.tracks = curve_is_string && curve->as_string().compare(0, 4, u"step") == 0;
    layout_options.straight = curve_is_string && curve->as_string() == u"linear";
    layout_options.head_room = edge_geometry::marker_trim("arrow_point");
    const layered_layouter engine(layout_options);
    const compound_result layout = compound_layouter(engine).layout(f.cnodes, f.cedges, f.root_dir);

    const label title = layouter.layout(db_.title ? ustring_view(*db_.title) : ustring_view(), false, f.size, tight ? fit_width::title_room(options_) : 4000, text_style::bold);
    const double title_height = title.height > 0 ? title.height + 12 : 0;
    const double inner = std::max(layout.width, title.width);
    const double ox = f.pad + (inner - layout.width) / 2;
    const double oy = f.pad + title_height;
    compound_layouter::shift(f.cnodes, f.cedges, ox, oy);
    const double width = std::ceil(inner + 2 * f.pad);
    const double height = std::ceil(layout.height + title_height + 2 * f.pad);

    crossing_map crossings;
    bool any_group = false;
    for (const compound_node& c : f.cnodes)
      if (c.is_group) any_group = true;
    if (any_group)
      for (const compound_edge& e : f.cedges) crossings.mark(e.route);

    std::string clusters;
    std::string struck_titles;
    std::string nodes_out;
    draw_nodes(f, crossings, clusters, struck_titles, nodes_out);

    std::string edges_out;
    std::string labels_out;
    draw_edges(f, edges_out, labels_out);

    accessible acc;
    if (db_.acc_title) acc.title = *db_.acc_title;
    if (db_.acc_descr) acc.description = *db_.acc_descr;
    const std::string content = svg::label_svg(title, width / 2, f.pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") +
                                (clusters.empty() ? "" : "<g class=\"pele-clusters\">" + clusters + "</g>") +
                                (edges_out.empty() ? "" : "<g class=\"pele-edges\" fill=\"none\" stroke=\"var(--_l)\" stroke-linecap=\"round\">" + edges_out + "</g>") +
                                (struck_titles.empty() ? "" : "<g class=\"pele-cluster-titles\">" + struck_titles + "</g>") +
                                (labels_out.empty() ? "" : "<g class=\"pele-edge-labels\" font-size=\"" + js_number_string(f.edge_size) + "\">" + labels_out + "</g>") +
                                "<g class=\"pele-nodes\">" + nodes_out + "</g>";
    rendered result;
    result.svg = svg_document::build("flowchart", width, height, f.size, options_, acc, content);
    result.width = width;
    result.height = height;
    result.links = std::move(f.links);
    return result;
  }

  void build_views(frame& f, const label_layouter& layouter) {
    std::unordered_map<ustring, int> index;
    for (std::size_t i = 0; i < f.graph.nodes.size(); i++) index[f.graph.nodes[i].id] = static_cast<int>(i);
    for (const graph_node& node : f.graph.nodes) {
      std::vector<ustring> styles = node.css_compiled_styles;
      if (!node.css_styles.empty()) styles.insert(styles.end(), node.css_styles.begin(), node.css_styles.end());
      node_view view;
      view.node = &node;
      view.style = theme::resolve_style(styles);
      const int text_style_flags = (view.style.bold ? text_style::bold : 0) | (view.style.italic ? text_style::italic : 0);
      const bool markdown = node.label_kind == label_type::markdown;
      const double font_size = view.style.font_size ? *view.style.font_size : f.size;
      compound_node c;
      const ustring_view label_text = node.label ? ustring_view(*node.label) : ustring_view();
      if (node.is_group) {
        view.text = layouter.layout(label_text, markdown, font_size, 4000, text_style_flags);
        view.shape = "group";
        c = compound_node(0, 0);
        c.is_group = true;
        if (node.dir) {
          c.has_dir = true;
          c.dir = parse_direction(*node.dir);
        }
        c.pad_x = f.group_pad;
        c.pad_top = view.text.height > 0 ? view.text.height + 16 : kGroupPad;
        c.pad_bottom = f.group_pad;
        c.min_w = view.text.width + 2 * f.group_pad - 12;
      } else {
        const std::string shape = canonical_shape(node.shape);
        const bool has_asset = (node.img && !node.img->empty()) || (node.icon && !node.icon->empty());
        const bool show_text = has_asset || shape_registry::instance().has_label(shape);
        view.text = layouter.layout(show_text ? label_text : ustring_view(), markdown, font_size, f.wrap_width, text_style_flags);
        double w, h, dy = 0;
        if (has_asset) {
          const double box = node.img && !node.img->empty() ? 80 : 48;
          const double aw = asset_size(node.asset_width, box);
          const double ah = asset_size(node.asset_height, box);
          w = std::max(aw, view.text.width + 8);
          h = ah + (view.text.height > 0 ? view.text.height + 6 : 0);
          dy = node.pos && *node.pos == u"t" ? -ah / 2 : ah / 2;
          if (view.text.height == 0) dy = 0;
        } else {
          const shape_size s = shape_registry::instance().size(shape, view.text.width, view.text.height);
          w = s.w;
          h = s.h;
          dy = s.dy;
        }
        view.shape = shape;
        view.w = w;
        view.h = h;
        view.dy = dy;
        c = compound_node(w, h);
      }
      c.seq = static_cast<double>(f.views.size());
      f.views.push_back(std::move(view));
      f.cnodes.push_back(c);
    }
    for (std::size_t i = 0; i < f.graph.nodes.size(); i++) {
      const graph_node& node = f.graph.nodes[i];
      if (node.parent_id) {
        const auto it = index.find(*node.parent_id);
        f.cnodes[i].parent = it != index.end() ? it->second : -1;
      } else {
        f.cnodes[i].parent = -1;
      }
    }
    index_ = std::move(index);
  }

  void set_spans(frame& f) {
    // The edges on one side of a node spread along it; a picture or an icon keeps them at its middle.
    for (std::size_t i = 0; i < f.views.size(); i++) {
      const node_view& view = f.views[i];
      const bool asset = (view.node->img && !view.node->img->empty()) || (view.node->icon && !view.node->icon->empty());
      if (view.node->is_group) f.cnodes[i].span = kInf;
      else if (!asset) f.cnodes[i].span = shape_registry::instance().span(view.shape, view.w, view.h, flows_sideways(f, i));
    }
  }

  void build_edges(frame& f, const label_layouter& layouter) {
    for (const graph_edge& edge : f.graph.edges) {
      const auto src = index_.find(edge.start);
      const auto dst = index_.find(edge.end);
      if (src == index_.end() || dst == index_.end()) continue;
      const label l = layouter.layout(edge.label, edge.label_kind == label_type::markdown, f.edge_size, f.wrap_width);
      if (src->second == dst->second) {
        // A self-loop sits beside the node, across the flow, and the node's box grows to hold it.
        f.views[src->second].loops++;
        if (flows_sideways(f, src->second)) f.cnodes[src->second].h += 2 * (kLoop + (l.height > 0 ? l.height + 8 : 0));
        else f.cnodes[src->second].w += 2 * (kLoop + (l.width > 0 ? l.width + 12 : 0));
      }
      compound_edge ce;
      ce.src = src->second;
      ce.dst = dst->second;
      ce.minlen = static_cast<int>(edge.minlen ? *edge.minlen : 1);
      ce.label_w = l.width > 0 ? l.width + 12 : 0;
      ce.label_h = l.height > 0 ? l.height + 4 : 0;
      f.drawn.push_back(&edge);
      f.edge_labels.push_back(l);
      f.cedges.push_back(std::move(ce));
    }
  }

  void draw_nodes(frame& f, const crossing_map& crossings, std::string& clusters, std::string& struck_titles, std::string& nodes_out) {
    for (std::size_t i = 0; i < f.views.size(); i++) {
      node_view& view = f.views[i];
      const compound_node& c = f.cnodes[i];
      const graph_node& node = *view.node;
      const double x = c.x;
      const double y = c.y;
      const std::string classes = theme::class_names(strip_default(node.css_classes));
      const std::string id = svg::esc_text(node.id);
      if (c.is_group) {
        const double title_x = crossings.title_x(c, view.text.width, f.group_pad);
        const double title_y = y - c.h / 2 + 8 + view.text.height / 2;
        const std::string title_attrs = " class=\"pele-cluster-label\" fill=\"var(--_m)\"" + view.style.text;
        const bool struck = crossings.crossed(c, view.text.width, title_x);
        if (struck) struck_titles += struck_title(view.text, title_x, title_y, title_attrs, id, f.cnodes, i, &icons_);
        clusters += "<g class=\"pele-cluster" + classes + "\" data-id=\"" + id + "\"><rect x=\"" + svg::num(x - c.w / 2) + "\" y=\"" + svg::num(y - c.h / 2) + "\" width=\"" +
                    svg::num(c.w) + "\" height=\"" + svg::num(c.h) + "\" fill=\"var(--_a)\" fill-opacity=\"0.5\" stroke=\"var(--_b)\"" +
                    theme::rounded(view.style.shape) + "/>" + (struck ? "" : svg::label_svg(view.text, title_x, title_y, title_attrs, &icons_)) + "</g>";
        continue;
      }

      const double w = view.w;
      const double h = view.h;
      std::string inner;
      if (node.img && !node.img->empty()) {
        const double aw = asset_size(node.asset_width, 80);
        const double ah = asset_size(node.asset_height, 80);
        const double top = view.text.height > 0 ? (node.pos && *node.pos == u"t" ? h / 2 - ah : -h / 2) : -ah / 2;
        const auto src = image_url(safe_url(*node.img), options_);
        if (src)
          inner = "<image href=\"" + svg::esc(*src) + "\" x=\"" + svg::num(-aw / 2) + "\" y=\"" + svg::num(top) + "\" width=\"" + svg::num(aw) + "\" height=\"" + svg::num(ah) +
                  "\" preserveAspectRatio=\"" + (node.constraint && *node.constraint == u"on" ? "xMidYMid meet" : "none") + "\"/>";
      } else if (node.icon && !node.icon->empty()) {
        const double box = asset_size(node.asset_height, 48);
        const double top = view.text.height > 0 ? (node.pos && *node.pos == u"t" ? h / 2 - box : -h / 2) : -box / 2;
        std::string frame_svg;
        if (node.shape == "icon") {
          frame_svg = "";
        } else if (node.shape == "iconCircle") {
          frame_svg = "<circle cy=\"" + svg::num(top + box / 2) + "\" r=\"" + svg::num(box / 2) + "\"" + kShapeAttrs + view.style.shape + "/>";
        } else {
          frame_svg = "<rect x=\"" + svg::num(-box / 2) + "\" y=\"" + svg::num(top) + "\" width=\"" + svg::num(box) + "\" height=\"" + svg::num(box) + "\" rx=\"" +
                      (node.shape == "iconRounded" ? "8" : "0") + "\"" + kShapeAttrs + view.style.shape + "/>";
        }
        const double glyph = box * 0.6;
        inner = frame_svg + "<svg class=\"pele-icon\" data-icon=\"" + svg::esc_text(*node.icon) + "\" x=\"" + svg::num(-glyph / 2) + "\" y=\"" + svg::num(top + (box - glyph) / 2) +
                "\" width=\"" + svg::num(glyph) + "\" height=\"" + svg::num(glyph) +
                "\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" +
                icons_.markup(to_utf8(*node.icon)) + "</svg>";
      } else {
        inner = shape_registry::instance().draw(view.shape, w, h, std::string(kShapeAttrs) + view.style.shape, std::string(kLineAttrs) + view.style.line);
      }

      std::string text = svg::label_svg(view.text, 0, view.dy, " class=\"pele-label\"" + view.style.text, &icons_);
      const bool internal = has_internal_link(classes);
      if (internal && node.label && !node.label->empty() && options_.links) {
        // The label is a note name, but it still ends up in an href, so it gets the same check as a URL.
        const ustring name = decode_entities(*node.label);
        const bool safe = link_url(sanitize_url(name), options_).has_value();
        const std::string target = svg::esc(name);
        text = "<a class=\"internal-link\"" + (safe ? " href=\"" + target + "\"" + rel_attr(options_) : std::string()) + " data-href=\"" + target + "\">" + text + "</a>";
        f.links.push_back({node.id, name, true});
      }
      std::string body = inner + text;
      if (node.tooltip && !node.tooltip->empty()) body = "<title>" + svg::esc_text(*node.tooltip) + "</title>" + body;
      if (node.link && !node.link->empty())
        body = linked(body, *node.link, node.id, f.links, options_, node.link_target && !node.link_target->empty() ? " target=\"" + svg::esc(*node.link_target) + "\"" : std::string());
      nodes_out += "<g class=\"pele-node pele-shape-" + view.shape + classes + "\" data-id=\"" + id + "\" transform=\"translate(" + svg::num(x) + "," + svg::num(y) + ")\">" + body +
                   "</g>";
    }
  }

  void draw_edges(frame& f, std::string& edges_out, std::string& labels_out) {
    side_reach reach;
    for (std::size_t i = 0; i < f.cedges.size(); i++) {
      const compound_edge& ce = f.cedges[i];
      const graph_edge& edge = *f.drawn[i];
      if (ce.src == ce.dst || (edge.thickness && *edge.thickness == "invisible") || ce.route.size() < 6) continue;
      reach.add(ce.src, end_side(ce.route, 0, f.cnodes[ce.src]), edge.arrow_type_start);
      reach.add(ce.dst, end_side(ce.route, ce.route.size() - 3, f.cnodes[ce.dst]), edge.arrow_type_end);
    }

    std::unordered_map<int, int> loop_seen;
    for (std::size_t i = 0; i < f.cedges.size(); i++) {
      compound_edge& ce = f.cedges[i];
      const graph_edge& edge = *f.drawn[i];
      const label& l = f.edge_labels[i];
      std::optional<theme::resolved_style> style;
      if (!edge.style.empty() || !edge.css_compiled_styles.empty()) {
        std::vector<ustring> styles = edge.css_compiled_styles;
        styles.insert(styles.end(), edge.style.begin(), edge.style.end());
        style = theme::resolve_style(styles);
      }
      const bool invisible = edge.thickness && *edge.thickness == "invisible";
      if (!invisible) {
        const std::string& start_type = edge.arrow_type_start;
        const std::string& end_type = edge.arrow_type_end;
        edge_path path;
        if (ce.src == ce.dst) {
          const node_view& view = f.views[ce.src];
          const int k = loop_seen.count(ce.src) ? loop_seen[ce.src] : 0;
          loop_seen[ce.src] = k + 1;
          loop_label ll;
          ll.label_w = ce.label_w;
          ll.label_h = ce.label_h;
          const compound_node& c = f.cnodes[ce.src];
          path = edge_geometry::loop_path(c.x, c.y, view.w, view.h, k, flows_sideways(f, ce.src), kLoop, edge_geometry::marker_trim(end_type), ll);
          ce.label_x = ll.label_x;
          ce.label_y = ll.label_y;
        } else {
          std::vector<double> route = ce.route;
          const double leaves = reach.of(ce.src, end_side(route, 0, f.cnodes[ce.src]));
          const double arrives = reach.of(ce.dst, end_side(route, route.size() - 3, f.cnodes[ce.dst]));
          inset_route(route, 0, shape_view{f.views[ce.src].shape, f.views[ce.src].w, f.views[ce.src].h}, f.cnodes[ce.src]);
          inset_route(route, route.size() - 3, shape_view{f.views[ce.dst].shape, f.views[ce.dst].w, f.views[ce.dst].h}, f.cnodes[ce.dst]);
          path = edge_geometry::route_path(route, edge.curve ? std::string_view(*edge.curve) : std::string_view(), edge_geometry::marker_trim(start_type),
                                           edge_geometry::marker_trim(end_type), leaves, arrives);
        }
        const std::string color = style && style->stroke && !style->stroke->empty() ? svg::esc(*style->stroke) : "var(--_l)";
        std::string attrs;
        if (edge.thickness && *edge.thickness == "thick") attrs += " stroke-width=\"2.5\"";
        else if (edge.thickness && *edge.thickness == "dotted") attrs += " stroke-dasharray=\"3 4\"";
        const std::string classes = theme::class_names(strip_edge_classes(edge.classes));
        edges_out += "<g class=\"pele-edge" + classes + "\" data-id=\"" + svg::esc_text(edge.id) + "\"" + (style ? style->line : std::string()) + "><path d=\"" + path.d + "\"" + attrs + "/>" +
                     edge_geometry::marker(start_type, path.sx, path.sy, path.sdx, path.sdy, color) + edge_geometry::marker(end_type, path.ex, path.ey, path.edx, path.edy, color) +
                     "</g>";
      }
      if (l.width > 0) labels_out += edge_geometry::label_svg(to_utf8(edge.id), l, ce.label_x, ce.label_y, style ? style->text : std::string(), &icons_);
    }
  }

  const flow_db& db_;
  const config& settings_;
  const render_options& options_;
  icon_set icons_;
  std::unordered_map<ustring, int> index_;
};

}  // namespace

rendered render_flowchart(const flow_db& db, const config& settings, const render_options& options) { return flowchart_renderer(db, settings, options).render(); }

}  // namespace pele::flowchart
