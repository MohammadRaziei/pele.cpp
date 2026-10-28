#include "class/render.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>

#include "chars.h"
#include "class/graph.h"
#include "class/markers.h"
#include "cluster.h"
#include "common/generics.h"
#include "components.h"
#include "compound.h"
#include "edges.h"
#include "entities.h"
#include "fit_width.h"
#include "js_math.h"
#include "label.h"
#include "shapes.h"
#include "svg.h"
#include "svg_document.h"
#include "theme.h"
#include "title.h"
#include "url.h"

namespace pele::classdiagram {
namespace {

constexpr double kNodeSep = 40;
constexpr double kEdgeSep = 16;
constexpr double kRankSep = 56;
constexpr double kGroupPad = 20;
constexpr double kLoop = 26;
constexpr double kPadX = 12;
constexpr double kTitlePad = 8;
constexpr double kSectionPad = 6;
constexpr double kEmptySection = 10;
constexpr double kMinWidth = 64;
constexpr double kFold = 8;
const char* const kLine = "var(--_l)";

using svg::esc;
using svg::esc_text;
using svg::num;

// A positive number set in the config, up to 2000, or the fallback.
double number_option(const yaml_value* section, const char* key, double fallback) {
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* value = section->find(key);
  return value != nullptr && value->is_number() && value->as_number() > 0 ? std::min(value->as_number(), 2000.0) : fallback;
}

std::string n(double v) { return num(v); }

struct line_view {
  ustring text;
  double width = 0;
  ustring classifier;
};

struct view {
  const graph_node* node = nullptr;
  theme::resolved_style style;
  label text;
  double w = 0;
  double h = 0;
  int loops = 0;
  std::vector<ustring> annotations;
  std::vector<line_view> attributes;
  std::vector<line_view> methods;
  double title_h = 0;
  double attributes_h = 0;
  double font_size = 0;
};

struct end_point {
  std::vector<double>* route = nullptr;
  std::size_t at = 0;
  double toward = 0;
  int order = 0;
  double room = 0;
  int side = 0;
  // Whether the text of this end goes on the right of, or below, its line.
  bool after = false;
};

struct card_view {
  ustring text;
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;
  int parent = -1;
};

ustring strip_default(const ustring& classes) {
  // /^default\s?/
  if (classes.compare(0, 7, u"default") != 0) return classes;
  std::size_t k = 7;
  if (k < classes.size() && is_space(classes[k])) k++;
  return classes.substr(k);
}

bool is_target(const ustring& t) { return t == u"_self" || t == u"_blank" || t == u"_parent" || t == u"_top"; }

ustring ascii_lowered(ustring s) {
  for (char16_t& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char16_t>(c + 32);
  return s;
}

class class_renderer {
 public:
  class_renderer(const class_model& db, const config& settings, const render_options& options) : db_(db), settings_(settings), options_(options), icons_(options.icons) {}

  rendered render() {
    std::optional<std::string> direction = to_utf8(db_.direction);
    auto fitted = [&](bool down) -> rendered { return fit_width::tighten(options_, [&](bool tight) { return draw(down, tight); }); };
    return fit_width::runs_across(direction) ? fit_width::turn_to_fit(options_, fitted) : fitted(false);
  }

 private:
  struct kept_entry {
    ustring prop;
    ustring decl;
  };

  rendered draw(bool turned, bool tight);

  // Only the last value of each property that has an effect is kept, in the order the properties first appear.
  void keep(const ustring& decl) {
    auto it = effect_.find(decl);
    if (it == effect_.end()) {
      const theme::resolved_style one = theme::resolve_style({decl});
      ustring prop;
      if (!(one.shape.empty() && one.text.empty())) {
        const std::size_t colon = decl.find(u':');
        const std::size_t end = colon == ustring::npos ? (decl.empty() ? 0 : decl.size() - 1) : colon;
        prop = ascii_lowered(trim(decl.substr(0, end)));
      }
      it = effect_.emplace(decl, prop).first;
    }
    if (it->second.empty()) return;
    for (kept_entry& e : kept_) {
      if (e.prop == it->second) {
        e.decl = decl;
        return;
      }
    }
    kept_.push_back({it->second, decl});
  }

  theme::resolved_style class_style(const class_node& cls) {
    kept_.clear();
    auto base = from_defs_.find(cls.css_classes);
    if (base == from_defs_.end()) {
      std::size_t from = 0;
      while (true) {
        const std::size_t at = cls.css_classes.find(u' ', from);
        const ustring name = cls.css_classes.substr(from, at == ustring::npos ? ustring::npos : at - from);
        const auto def = db_.style_class_index.find(name);
        if (def != db_.style_class_index.end()) {
          for (const ustring& s : db_.style_classes[def->second].styles) {
            std::size_t p = 0;
            while (true) {
              const std::size_t comma = s.find(u',', p);
              keep(s.substr(p, comma == ustring::npos ? ustring::npos : comma - p));
              if (comma == ustring::npos) break;
              p = comma + 1;
            }
          }
        }
        if (at == ustring::npos) break;
        from = at + 1;
      }
      base = from_defs_.emplace(cls.css_classes, kept_).first;
    } else {
      for (const kept_entry& e : base->second) {
        bool found = false;
        for (kept_entry& k : kept_) {
          if (k.prop == e.prop) {
            k.decl = e.decl;
            found = true;
            break;
          }
        }
        if (!found) kept_.push_back(e);
      }
    }
    for (const ustring& s : cls.styles) keep(s);
    std::vector<ustring> decls;
    for (const kept_entry& e : kept_) decls.push_back(e.decl);
    return theme::resolve_style(decls);
  }

  line_view member_line(const class_member& member, const text_measurer& measurer, double size) const {
    line_view line;
    line.text = trim(decode_entities(member.display_details().text));
    line.classifier = member.classifier;
    line.width = measurer.width(line.text, size, member.classifier == u"*" ? text_style::italic : 0);
    return line;
  }

  const class_model& db_;
  const config& settings_;
  const render_options& options_;
  icon_set icons_;
  std::map<ustring, ustring> effect_;
  std::map<ustring, std::vector<kept_entry>> from_defs_;
  std::vector<kept_entry> kept_;
};

rendered class_renderer::draw(bool turned, bool tight) {
  const yaml_value* section = settings_.find("class");
  const yaml_value* conf = section != nullptr && section->is_object() ? section : nullptr;
  bool hierarchical = true;
  bool hide_empty = false;
  if (conf != nullptr) {
    const yaml_value* h = conf->find("hierarchicalNamespaces");
    if (h != nullptr && h->is_boolean() && !h->as_boolean()) hierarchical = false;
    const yaml_value* e = conf->find("hideEmptyMembersBox");
    if (e != nullptr && e->is_boolean() && e->as_boolean()) hide_empty = true;
  }
  const class_graph graph = build_class_graph(db_, hierarchical);
  const double size = options_.font_size ? *options_.font_size : 16;
  const double small = js_round(size * 0.875);
  const double tiny = js_round(size * 0.8);
  const double tiny_height = js_round(tiny * 1.4);
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options_.measurer != nullptr ? *options_.measurer : default_measurer;
  const label_layouter layouter(measurer, &icons_);
  const double pad = options_.padding ? *options_.padding : 8;
  auto card_width = [&](const ustring& raw) { return raw.empty() ? 0.0 : measurer.width(decode_entities(raw), tiny, 0); };
  const flow_dir root_dir = turned ? flow_dir::tb : parse_direction(to_utf8(db_.direction));
  const bool vertical_root = root_dir == flow_dir::tb || root_dir == flow_dir::bt;

  const std::size_t count = graph.nodes.size();
  std::vector<int> children(count, 0);
  for (const graph_node& node : graph.nodes)
    if (node.parent >= 0) children[node.parent]++;

  // Namespaces that an edge may cross into are made wide enough for the title to sit beside it.
  std::vector<std::uint8_t> crossed(count, 0);
  for (const graph_edge& edge : graph.edges) {
    if (edge.source < 0 || edge.target < 0) continue;
    const int a = graph.nodes[edge.source].parent;
    const int b = graph.nodes[edge.target].parent;
    if (a == b) continue;
    for (int g = a; g >= 0 && crossed[g] == 0; g = graph.nodes[g].parent) crossed[g] = 1;
    for (int g = b; g >= 0 && crossed[g] == 0; g = graph.nodes[g].parent) crossed[g] = 1;
  }

  std::vector<view> views;
  std::vector<compound_node> cnodes;
  auto base = [&](const graph_node& node, const label& l, double w, double h, double font_size) {
    view v;
    v.node = &node;
    v.style = theme::resolve_style({});
    v.text = l;
    v.w = w;
    v.h = h;
    v.font_size = font_size;
    return v;
  };
  auto class_view = [&](const graph_node& node, const class_node& cls) {
    const theme::resolved_style style = class_style(cls);
    const double font_size = style.font_size ? *style.font_size : size;
    const double member_size = js_round(font_size * 0.875);
    const double line_height = js_round(member_size * 1.5);
    // The generic type is escaped so that `<T>` is not read as a tag.
    ustring generic;
    if (!cls.type.empty()) {
      ustring inner;
      for (char16_t c : parse_generic_types(cls.type)) {
        if (c == '<') inner += u"&lt;";
        else if (c == '>') inner += u"&gt;";
        else inner += c;
      }
      generic = u"&lt;" + inner + u"&gt;";
    }
    const label l = layouter.layout(cls.label + generic, false, font_size, 4000, text_style::bold | (style.italic ? text_style::italic : 0));
    view v;
    v.node = &node;
    v.style = style;
    v.text = l;
    v.font_size = font_size;
    for (const ustring& a : cls.annotations) v.annotations.push_back(u"«" + trim(decode_entities(a)) + u"»");
    for (const class_member& m : cls.members) v.attributes.push_back(member_line(m, measurer, member_size));
    for (const class_member& m : cls.methods) v.methods.push_back(member_line(m, measurer, member_size));
    double inner = l.width;
    double annotations_h = 0;
    for (const ustring& a : v.annotations) {
      inner = std::max(inner, measurer.width(a, tiny, 0));
      annotations_h += tiny_height;
    }
    for (const line_view& line : v.attributes) inner = std::max(inner, line.width);
    for (const line_view& line : v.methods) inner = std::max(inner, line.width);
    const bool sections = !(hide_empty && v.attributes.empty() && v.methods.empty());
    auto section_h = [&](std::size_t lines) { return !sections ? 0.0 : lines > 0 ? 2 * kSectionPad + static_cast<double>(lines) * line_height : kEmptySection; };
    v.title_h = 2 * kTitlePad + annotations_h + std::max(l.height, js_round(font_size * 1.5));
    v.attributes_h = section_h(v.attributes.size());
    v.w = std::max(inner + 2 * kPadX, kMinWidth);
    v.h = v.title_h + v.attributes_h + section_h(v.methods.size());
    return v;
  };
  for (std::size_t i = 0; i < count; i++) {
    const graph_node& node = graph.nodes[i];
    view v;
    compound_node c;
    if (node.is_group) {
      const label l = layouter.layout(node.label, false, size, 4000);
      v = base(node, l, 0, 0, size);
      if (children[i] > 0) {
        c = compound_node(0, 0);
        c.is_group = true;
        c.pad_x = kGroupPad;
        c.pad_top = l.height > 0 ? l.height + 16 : kGroupPad;
        c.pad_bottom = kGroupPad;
        c.min_w = l.width + 2 * kGroupPad - 12 + (crossed[i] == 1 && !tight ? std::min(l.width + 24, 240.0) : 0);
      } else {
        v.w = std::max(l.width + 2 * kGroupPad, 80.0);
        v.h = l.height + 2 * kGroupPad;
        c = compound_node(v.w, v.h);
      }
    } else if (node.cls != nullptr) {
      v = class_view(node, *node.cls);
      c = compound_node(v.w, v.h);
    } else if (node.shape == node_shape::note) {
      const label l = layouter.layout(node.label, true, small, 260);
      v = base(node, l, std::max(l.width + 2 * kPadX, 40.0), l.height + 2 * kTitlePad, small);
      c = compound_node(v.w, v.h);
    } else {
      const label l = layouter.layout(node.label, false, small, 200);
      v = base(node, l, std::max(l.width, 12.0) + 4, std::max(l.height, 12.0), small);
      c = compound_node(v.w, v.h);
    }
    c.parent = node.parent;
    c.seq = static_cast<double>(i);
    views.push_back(std::move(v));
    cnodes.push_back(c);
  }

  std::vector<compound_edge> cedges;
  std::vector<const graph_edge*> drawn;
  std::vector<label> edge_labels;
  std::vector<double> start_room;
  std::vector<double> end_room;
  // Edges leave one side of a class and arrive at the other, so a class with many of them
  // is widened until they, and the text at their ends, fit side by side.
  std::vector<double> crowd(views.size() * 4, 0.0);
  auto meet = [&](int item, int slot, double room) {
    crowd[item * 4 + slot]++;
    if (room > crowd[item * 4 + slot + 1]) crowd[item * 4 + slot + 1] = room;
  };
  for (const graph_edge& edge : graph.edges) {
    if (edge.source < 0 || edge.target < 0) continue;
    const label l = layouter.layout(edge.label, true, small, 200);
    const double start_w = card_width(edge.start_label_right);
    const double end_w = card_width(edge.end_label_left);
    if (edge.source == edge.target) {
      views[edge.source].loops++;
      cnodes[edge.source].w += 2 * (kLoop + (l.width > 0 ? l.width + 12 : 0));
    } else {
      meet(edge.source, 0, start_w);
      meet(edge.target, 2, end_w);
    }
    start_room.push_back(start_w);
    end_room.push_back(end_w);
    drawn.push_back(&edge);
    edge_labels.push_back(l);
    compound_edge ce;
    ce.src = edge.source;
    ce.dst = edge.target;
    ce.minlen = 1;
    ce.label_w = l.width > 0 ? l.width + 12 : 0;
    ce.label_h = l.height > 0 ? l.height + 4 : 0;
    cedges.push_back(std::move(ce));
  }

  if (vertical_root) {
    for (std::size_t i = 0; i < views.size(); i++) {
      if (views[i].node->cls == nullptr) continue;
      double need = 0;
      for (int slot = 0; slot < 4; slot += 2) {
        const double cnt = crowd[i * 4 + slot];
        const double room = crowd[i * 4 + slot + 1];
        if (cnt > 1) need = std::max(need, (cnt - 1) * (room > 0 ? room + 24 : 14) + 24);
      }
      need = std::min(need, 480.0);
      if (need > views[i].w) {
        cnodes[i].w += need - views[i].w;
        views[i].w = need;
      }
    }
  }

  bool lettered = false;
  for (std::size_t i = 0; i < drawn.size() && !lettered; i++) lettered = start_room[i] > 0 || end_room[i] > 0;
  // Cardinalities at both ends of a vertical edge need room for their height between the ranks.
  double tall_cards = 0;
  if (vertical_root) {
    auto card_height = [&](const ustring& raw) { return raw.empty() ? 0.0 : tiny_height; };
    for (const graph_edge* edge : drawn) tall_cards = std::max(tall_cards, card_height(edge->start_label_right) + card_height(edge->end_label_left) - 2 * tiny_height);
  }
  layered_options layout_options;
  layout_options.node_sep = number_option(conf, "nodeSpacing", kNodeSep);
  layout_options.edge_sep = kEdgeSep;
  layout_options.rank_sep = number_option(conf, "rankSpacing", lettered ? kRankSep + 16 : kRankSep) + tall_cards;
  layout_options.port_sep = 20;
  layout_options.apart = true;
  const layered_layouter engine(layout_options);
  const compound_result layout = compound_layouter(engine).layout(cnodes, cedges, root_dir);

  // Every edge reaches the middle of a side. Where several share a side they are spread along it,
  // in the order of where they come from. An end with text runs straight for the length of the text,
  // which sits beside it on the side the line then bends toward, away from the neighbors that bend
  // the other way. Neighbors are set apart far enough for the text between them.
  std::deque<end_point> store;
  std::map<int, std::vector<end_point*>> sides;
  std::vector<int> side_order;
  std::vector<card_view> cards;
  auto attach = [&](compound_edge& ce, std::size_t at, int item, double room, int order) -> end_point* {
    std::vector<double>& route = ce.route;
    const compound_node& c = cnodes[item];
    const int side = end_side(route, at, c);
    if (side < 0) return nullptr;
    const double slack = (c.w - views[item].w) / 2;
    if (side == 1) route[at] -= slack;
    else if (side == 3) route[at] += slack;
    const int across = side & 1;
    const double toward = route[(at == 0 ? 3 : at - 3) + across];
    const double bend = toward - route[at + across];
    store.push_back({&route, at, toward, order, room, side, across ? bend > 0 : bend >= 0});
    end_point* end = &store.back();
    const int key = item * 4 + side;
    auto it = sides.find(key);
    if (it == sides.end()) {
      side_order.push_back(key);
      sides[key].push_back(end);
    } else {
      it->second.push_back(end);
    }
    return end;
  };
  std::vector<end_point*> starts;
  std::vector<end_point*> ends;
  for (std::size_t i = 0; i < cedges.size(); i++) {
    compound_edge& ce = cedges[i];
    const bool routed = ce.src != ce.dst && ce.route.size() >= 6;
    starts.push_back(routed ? attach(ce, 0, ce.src, start_room[i], static_cast<int>(i)) : nullptr);
    ends.push_back(routed ? attach(ce, ce.route.size() - 3, ce.dst, end_room[i], static_cast<int>(i)) : nullptr);
  }
  // Whether a point lies on the border of one of the namespaces around a class.
  auto on_border = [&](int item, double x, double y) {
    int hops = 0;
    for (int g = cnodes[item].parent; g >= 0 && hops < 10000; g = cnodes[g].parent, hops++) {
      const compound_node& c = cnodes[g];
      const double dx = std::abs(x - c.x);
      const double dy = std::abs(y - c.y);
      if ((std::abs(dy - c.h / 2) < 0.5 && dx <= c.w / 2 + 0.5) || (std::abs(dx - c.w / 2) < 0.5 && dy <= c.h / 2 + 0.5)) return true;
    }
    return false;
  };
  std::vector<double> gaps;
  for (int key : side_order) {
    std::vector<end_point*>& list = sides[key];
    if (list.size() < 2) continue;
    const int item = key >> 2;
    const int across = key & 1;
    const double length = across ? views[item].h : views[item].w;
    std::stable_sort(list.begin(), list.end(), [](const end_point* a, const end_point* b) {
      const double d = a->toward - b->toward;
      if (d != 0 && !std::isnan(d)) return d < 0;
      return a->order < b->order;
    });
    double total = 0;
    gaps.assign(list.size(), 0.0);
    for (std::size_t j = 0; j + 1 < list.size(); j++) {
      const end_point* a = list[j];
      const end_point* b = list[j + 1];
      double gap = across ? 20 : 24;
      if (a->room > 0 && a->after) gap += across ? tiny_height + 6 : a->room;
      if (b->room > 0 && !b->after) gap += across ? tiny_height + 6 : b->room;
      gaps[j] = gap;
      total += gap;
    }
    const double scale = std::min(1.0, (length - 24) / total);
    if (scale <= 0) continue;
    double at = (across ? cnodes[item].y : cnodes[item].x) - (total * scale) / 2;
    for (std::size_t j = 0; j < list.size(); j++) {
      std::vector<double>& route = *list[j]->route;
      const long long end = static_cast<long long>(list[j]->at);
      // An edge that crosses the border of the class's namespace close by runs straight to it:
      // the point where it crosses moves into line with its end.
      const long long step = end == 0 ? 3 : -3;
      const long long first = end + step;
      const long long rn = static_cast<long long>(route.size());
      long long border = -1;
      for (long long k = first; k >= 0 && k < rn && std::abs(route[k + across] - route[first + across]) < 0.5; k += step) {
        if (on_border(item, route[k], route[k + 1])) {
          border = k;
          break;
        }
      }
      if (border >= 0 && std::abs(route[first + across] - at) <= 24)
        for (long long k = first; k != border + step; k += step) route[k + across] = at;
      route[end + across] = at;
      at += gaps[j] * scale;
    }
  }

  auto straighten = [&](std::vector<double>& route, const end_point* end, double room, bool first) {
    if (end == nullptr || room <= 0) return;
    const int across = end->side & 1;
    const std::size_t at = first ? 0 : route.size() - 3;
    const std::size_t along = at + 1 - across;
    const double reach = std::abs(route[(first ? 3 : at - 3) + 1 - across] - route[along]);
    const double length = std::min(across ? room + 10 : tiny_height + 6, reach * 0.35);
    if (length < 4) return;
    double point[3] = {route[at], route[at + 1], route[at + 2]};
    point[1 - across] += (end->side == 0 || end->side == 3 ? -1 : 1) * length;
    const std::size_t where = first ? 3 : at;
    route.insert(route.begin() + static_cast<std::ptrdiff_t>(where), point, point + 3);
  };
  for (std::size_t i = 0; i < cedges.size(); i++) {
    straighten(cedges[i].route, starts[i], start_room[i], true);
    straighten(cedges[i].route, ends[i], end_room[i], false);
  }

  double min_x = 0;
  double min_y = 0;
  double max_x = layout.width;
  double max_y = layout.height;
  auto card = [&](int parent, const ustring& raw, double width, double x, double y, int side, bool after) {
    if (raw.empty()) return;
    const double h = tiny_height;
    double cx = x;
    double cy = y;
    if (side & 1) {
      cx += (side == 3 ? -1 : 1) * (5 + width / 2);
      cy += (after ? 1 : -1) * (7 + h / 2);
    } else {
      cx += (after ? 1 : -1) * (9 + width / 2);
      cy += (side == 0 ? -1 : 1) * (3 + h / 2);
    }
    cards.push_back({decode_entities(raw), cx, cy, width, h, parent});
    min_x = std::min(min_x, cx - width / 2);
    max_x = std::max(max_x, cx + width / 2);
    min_y = std::min(min_y, cy - h / 2);
    max_y = std::max(max_y, cy + h / 2);
  };
  std::map<int, int> loop_seen;
  std::vector<int> loop_at(cedges.size(), 0);
  for (std::size_t i = 0; i < cedges.size(); i++) {
    compound_edge& ce = cedges[i];
    const graph_edge& edge = *drawn[i];
    const std::vector<double>& route = ce.route;
    if (ce.src == ce.dst) {
      const int k = loop_seen[ce.src];
      loop_seen[ce.src] = k + 1;
      loop_at[i] = k;
      const compound_node& c = cnodes[ce.src];
      const view& v = views[ce.src];
      const double x = c.x + v.w / 2;
      const double spread = std::min(v.h / 2 - 4, 8.0 + k * 6);
      card(ce.src, edge.start_label_right, start_room[i], x, c.y - spread, 1, false);
      card(ce.dst, edge.end_label_left, end_room[i], x, c.y + spread, 1, true);
      ce.label_x = c.x + v.w / 2 + kLoop + k * 8 + 4 + ce.label_w / 2;
      ce.label_y = c.y;
    } else if (route.size() >= 6) {
      const std::size_t last = route.size() - 3;
      const end_point* from = starts[i];
      const end_point* to = ends[i];
      card(ce.src, edge.start_label_right, start_room[i], route[0], route[1], from ? from->side : 2, from ? from->after : true);
      card(ce.dst, edge.end_label_left, end_room[i], route[last], route[last + 1], to ? to->side : 0, to ? to->after : true);
    }
  }

  if (options_.max_width && *options_.max_width > 0 && max_x - min_x + 2 * pad > *options_.max_width) {
    std::vector<compound_node> all = cnodes;
    for (const card_view& item : cards) {
      compound_node c(item.w, item.h, item.parent);
      c.x = item.x;
      c.y = item.y;
      all.push_back(c);
    }
    const std::optional<compound_result> wrapped = wrap_components(all, cedges, std::max(1.0, *options_.max_width - 2 * pad), number_option(conf, "nodeSpacing", kNodeSep));
    if (wrapped) {
      for (std::size_t i = 0; i < cnodes.size(); i++) cnodes[i] = all[i];
      for (std::size_t i = 0; i < cards.size(); i++) {
        cards[i].x = all[cnodes.size() + i].x;
        cards[i].y = all[cnodes.size() + i].y;
      }
      min_x = min_y = 0;
      max_x = wrapped->width;
      max_y = wrapped->height;
    }
  }

  const double ox = pad - min_x;
  const double oy = pad - min_y;
  const double width = std::ceil(max_x - min_x + 2 * pad);
  const double height = std::ceil(max_y - min_y + 2 * pad);
  std::vector<rendered_link> links;

  crossing_map crossings;
  if (!graph.nodes.empty() && graph.nodes[0].is_group)
    for (const compound_edge& e : cedges) crossings.mark(e.route);

  auto class_svg = [&](const view& v, const class_node& cls, const std::string& id, const std::string& at) {
    const double w = v.w;
    const double h = v.h;
    const theme::resolved_style& style = v.style;
    const double left = -w / 2;
    const double top = -h / 2;
    const double member_size = js_round(v.font_size * 0.875);
    const double line_height = js_round(member_size * 1.5);
    std::string body = "<rect x=\"" + n(left) + "\" y=\"" + n(top) + "\" width=\"" + n(w) + "\" height=\"" + n(h) + "\" fill=\"var(--_s)\" stroke=\"var(--_b)\"" + theme::rounded(style.shape) + "/>";
    const double first = top + v.title_h;
    const double second = first + v.attributes_h;
    if (h > v.title_h) {
      const std::string stroke = style.stroke ? " style=\"stroke:" + esc(*style.stroke) + "\"" : "";
      body += "<path class=\"pele-divider\" d=\"M" + n(left) + "," + n(first) + "H" + n(-left) + "M" + n(left) + "," + n(second) + "H" + n(-left) + "\" fill=\"none\" stroke=\"var(--_b)\"" + stroke + "/>";
    }
    double y = top + kTitlePad;
    for (const ustring& annotation : v.annotations) {
      body += "<text class=\"pele-annotation\" x=\"0\" y=\"" + n(y + tiny_height / 2 + tiny * 0.35) + "\" text-anchor=\"middle\" font-size=\"" + n(tiny) + "\" fill=\"var(--_m)\">" +
              esc(annotation) + "</text>";
      y += tiny_height;
    }
    const std::string title_size = v.font_size == size ? "" : " font-size=\"" + n(v.font_size) + "\"";
    body += svg::label_svg(v.text, 0, (y + first - kTitlePad) / 2, " class=\"pele-label\"" + title_size + " font-weight=\"var(--_hw)\"" + style.text, &icons_);
    auto lines = [&](const std::vector<line_view>& list, const std::string& kind, double from) {
      std::string out;
      double baseline = from + kSectionPad + line_height / 2 + member_size * 0.35;
      for (const line_view& line : list) {
        std::string mark;
        if (line.classifier == u"$") {
          mark = " pele-static\"" + (!style.text.empty() ? style.text.substr(0, style.text.size() - 1) + "text-decoration:underline\"" : std::string(" style=\"text-decoration:underline\""));
        } else if (line.classifier == u"*") {
          mark = " pele-abstract\" font-style=\"italic\"" + style.text;
        } else {
          mark = "\"" + style.text;
        }
        out += "<text class=\"pele-member pele-" + kind + mark + " x=\"" + n(left + kPadX) + "\" y=\"" + n(baseline) + "\">" + esc(line.text) + "</text>";
        baseline += line_height;
      }
      return out;
    };
    if (!v.attributes.empty() || !v.methods.empty())
      body += "<g class=\"pele-members\" font-size=\"" + n(member_size) + "\">" + lines(v.attributes, "attribute", first) + lines(v.methods, "method", second) + "</g>";
    if (cls.tooltip && !cls.tooltip->empty()) body = "<title>" + esc_text(*cls.tooltip) + "</title>" + body;
    if (cls.link && !cls.link->empty()) {
      const bool target = cls.link_target && !cls.link_target->empty() && is_target(*cls.link_target);
      body = linked(body, *cls.link, cls.id, links, options_, target ? " target=\"" + to_utf8(*cls.link_target) + "\"" : std::string());
    }
    return "<g class=\"pele-node pele-class" + theme::class_names(strip_default(cls.css_classes)) + "\" data-id=\"" + id + "\"" + at + ">" + body + "</g>";
  };

  std::string clusters;
  std::string struck_titles;
  std::string nodes_out;
  for (std::size_t i = 0; i < views.size(); i++) {
    const view& v = views[i];
    const compound_node& c = cnodes[i];
    const graph_node& node = *v.node;
    const double x = c.x + ox;
    const double y = c.y + oy;
    const std::string id = esc_text(node.id);
    const double w = c.is_group ? c.w : v.w;
    const double h = c.is_group ? c.h : v.h;
    if (node.is_group) {
      const double title_x = c.is_group ? crossings.title_x(c, v.text.width, kGroupPad) : c.x;
      const double title_y = y - h / 2 + 8 + v.text.height / 2;
      const std::string title_attrs = " class=\"pele-cluster-label\" fill=\"var(--_m)\"";
      const bool struck = c.is_group && crossings.crossed(c, v.text.width, title_x);
      if (struck) struck_titles += struck_title(v.text, title_x + ox, title_y, title_attrs, id, cnodes, i);
      clusters += "<g class=\"pele-cluster pele-namespace\" data-id=\"" + id + "\"><rect x=\"" + n(x - w / 2) + "\" y=\"" + n(y - h / 2) + "\" width=\"" + n(w) + "\" height=\"" + n(h) +
                  "\" fill=\"var(--_a)\" fill-opacity=\"0.5\" stroke=\"var(--_b)\"" + theme::kRounded + "/>" + (struck ? "" : svg::label_svg(v.text, title_x + ox, title_y, title_attrs)) + "</g>";
      continue;
    }
    const std::string at = " transform=\"translate(" + n(x) + "," + n(y) + ")\"";
    if (node.cls != nullptr) {
      nodes_out += class_svg(v, *node.cls, id, at);
    } else if (node.shape == node_shape::note) {
      const double r = w / 2;
      const double t = -h / 2;
      nodes_out += "<g class=\"pele-node pele-note\" data-id=\"" + id + "\"" + at + "><path d=\"M" + n(-r) + "," + n(t) + "H" + n(r - kFold) + "L" + n(r) + "," + n(t + kFold) + "V" + n(-t) + "H" +
                   n(-r) + "Z\" fill=\"var(--_a)\" stroke=\"var(--_b)\" stroke-linejoin=\"round\"/><path d=\"M" + n(r - kFold) + "," + n(t) + "V" + n(t + kFold) + "H" + n(r) +
                   "\" fill=\"none\" stroke=\"var(--_b)\" stroke-linejoin=\"round\"/>" + svg::label_svg(v.text, 0, 0, " class=\"pele-label\" font-size=\"" + n(small) + "\"", &icons_) + "</g>";
    } else {
      nodes_out += "<g class=\"pele-node pele-interface\" data-id=\"" + id + "\"" + at + ">" + svg::label_svg(v.text, 0, 0, " class=\"pele-label\" font-size=\"" + n(small) + "\"", &icons_) + "</g>";
    }
  }

  std::string edges_out;
  std::string labels_out;
  for (std::size_t i = 0; i < cedges.size(); i++) {
    compound_edge& ce = cedges[i];
    const graph_edge& edge = *drawn[i];
    const label& l = edge_labels[i];
    const end_marker start_type = edge.arrow_start;
    const end_marker end_type = edge.arrow_end;
    const double start_trim = class_marker_trim(start_type);
    const double end_trim = class_marker_trim(end_type);
    edge_path path;
    if (ce.src == ce.dst) {
      const compound_node& c = cnodes[ce.src];
      const view& v = views[ce.src];
      const int k = loop_at[i];
      const double x = c.x + ox + v.w / 2;
      const double y = c.y + oy;
      const double spread = std::min(v.h / 2 - 4, 8.0 + k * 6);
      const double reach = kLoop + k * 8;
      path.d = "M" + n(x + start_trim) + "," + n(y - spread) + "C" + n(x + reach + start_trim) + "," + n(y - spread) + " " + n(x + reach + end_trim) + "," + n(y + spread) + " " + n(x + end_trim) + "," +
               n(y + spread);
      path.sx = x;
      path.sy = y - spread;
      path.sdx = -1;
      path.sdy = 0;
      path.ex = x;
      path.ey = y + spread;
      path.edx = -1;
      path.edy = 0;
    } else {
      std::vector<double>& route = ce.route;
      for (std::size_t k = 0; k + 1 < route.size(); k += 3) {
        route[k] += ox;
        route[k + 1] += oy;
      }
      path = edge_geometry::route_path(route, "", start_trim, end_trim);
    }
    const std::string attrs = edge.pattern == edge_pattern::dashed ? " stroke-dasharray=\"6 4\"" : edge.pattern == edge_pattern::dotted ? " stroke-dasharray=\"2 4\"" : "";
    edges_out += std::string("<g class=\"pele-edge") + (edge.pattern == edge_pattern::dotted ? " pele-note-edge" : " pele-relation") + "\" data-id=\"" + esc_text(edge.id) + "\"><path d=\"" + path.d + "\"" + attrs + "/>" +
                 class_marker(start_type, path.sx, path.sy, path.sdx, path.sdy, kLine) + class_marker(end_type, path.ex, path.ey, path.edx, path.edy, kLine) + "</g>";
    if (l.width > 0) labels_out += edge_geometry::label_svg(to_utf8(edge.id), l, ce.label_x + ox, ce.label_y + oy, "", &icons_);
  }

  std::string cards_out;
  for (const card_view& item : cards)
    cards_out += "<text class=\"pele-cardinality\" x=\"" + n(item.x + ox) + "\" y=\"" + n(item.y + oy + tiny * 0.35) + "\" text-anchor=\"middle\">" + esc(item.text) + "</text>";

  const std::string content = (clusters.empty() ? "" : "<g class=\"pele-clusters\">" + clusters + "</g>") +
                              (edges_out.empty() ? "" : std::string("<g class=\"pele-edges\" fill=\"none\" stroke=\"") + kLine + "\" stroke-linecap=\"round\">" + edges_out + "</g>") +
                              (struck_titles.empty() ? "" : "<g class=\"pele-cluster-titles\">" + struck_titles + "</g>") +
                              (labels_out.empty() ? "" : "<g class=\"pele-edge-labels\" font-size=\"" + n(small) + "\">" + labels_out + "</g>") +
                              (cards_out.empty() ? "" : "<g class=\"pele-cardinalities\" font-size=\"" + n(tiny) + "\" fill=\"var(--_m)\">" + cards_out + "</g>") + "<g class=\"pele-nodes\">" + nodes_out + "</g>";
  const titled with = with_title(db_.title ? ustring_view(*db_.title) : ustring_view(), layouter, size, pad, width, height, content);
  accessible acc;
  if (db_.acc_title) acc.title = *db_.acc_title;
  if (db_.acc_descr) acc.description = *db_.acc_descr;
  rendered result;
  result.svg = svg_document::build("class", with.width, with.height, size, options_, acc, with.content);
  result.width = with.width;
  result.height = with.height;
  result.links = std::move(links);
  return result;
}

}  // namespace

rendered render_class(const class_model& model, const config& settings, const render_options& options) { return class_renderer(model, settings, options).render(); }

}  // namespace pele::classdiagram
