#include "cynefin/cynefin.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "common/tokens.h"
#include "cynefin/boundaries.h"
#include "edges.h"
#include "js_math.h"
#include "label.h"
#include "svg.h"
#include "svg_document.h"
#include "text_measurer.h"
#include "theme.h"

namespace pele::cynefin {
namespace {

// The order Pele numbers the tokens in.
enum token : int { T_keywordColon, T_keyword, T_arrow, T_colon, T_domain, T_accDescr, T_accTitle, T_title, T_string, T_newline };

const token_set& cynefin_tokens() {
  static const token_set set({
      token_def::literal("cynefin-beta:", u"cynefin-beta:"),
      common_tokens::keyword("cynefin-beta"),
      token_def::literal("-->", u"-->"),
      token_def::literal(":", u":"),
      token_def::regex("DOMAIN_NAME", R"(complex|complicated|clear|chaotic|confusion)"),
      common_tokens::acc_descr(),
      common_tokens::acc_title(),
      common_tokens::title(),
      common_tokens::string(),
      common_tokens::newline(),
      common_tokens::whitespace(),
      common_tokens::yaml(),
      common_tokens::directive(),
      common_tokens::single_line_comment(),
  });
  return set;
}

}  // namespace

cynefin_ast parse_cynefin(const ustring& src) {
  const token_set& types = cynefin_tokens();
  reader r(tokenize(src, types, "cynefin"), types, "cynefin");
  cynefin_ast ast;

  const auto skip_lines = [&]() {
    while (r.kind_at(r.i) == T_newline) r.i++;
  };
  const auto end_of_line = [&]() {
    if (r.kind() == -1) return;
    if (r.kind() != T_newline) r.fail("a line break or the end of input");
    skip_lines();
  };

  skip_lines();
  if (r.kind() != T_keyword && r.kind() != T_keywordColon) r.fail("'cynefin-beta'");
  r.i++;
  while (r.kind() != -1) {
    switch (r.kind()) {
      case T_newline: r.i++; break;
      case T_accDescr:
        ast.acc_descr = common_tokens::acc_descr_value(r.take());
        end_of_line();
        break;
      case T_accTitle:
        ast.acc_title = common_tokens::acc_title_value(r.take());
        end_of_line();
        break;
      case T_title:
        ast.title = common_tokens::title_value(r.take());
        end_of_line();
        break;
      case T_domain: {
        const std::string name = to_utf8(r.take());
        if (r.accept(T_arrow)) {
          transition_ast transition;
          transition.from = name;
          transition.to = to_utf8(r.expect(T_domain));
          if (r.accept(T_colon)) transition.label = common_tokens::string_value(r.expect(T_string));
          end_of_line();
          ast.transitions.push_back(std::move(transition));
          break;
        }
        domain_block_ast block;
        block.domain = name;
        skip_lines();
        while (r.kind_at(r.i) == T_string) {
          block.items.push_back(common_tokens::string_value(r.take()));
          skip_lines();
        }
        ast.domains.push_back(std::move(block));
        break;
      }
      default: r.fail("a domain, a transition, a title, or a line break");
    }
  }
  return ast;
}

cynefin_model build_cynefin(const cynefin_ast& ast, const std::optional<ustring>& title) {
  cynefin_model model;
  // A later block for a domain replaces an earlier one.
  for (const domain_block_ast& block : ast.domains) model.domains[block.domain] = block.items;
  for (const transition_ast& t : ast.transitions) {
    if (t.from == t.to) continue;
    cynefin_transition out;
    out.from = t.from;
    out.to = t.to;
    if (t.label && !t.label->empty()) out.label = t.label;
    model.transitions.push_back(std::move(out));
  }
  // `ast.title || title`
  model.title = ast.title && !ast.title->empty() ? ast.title : title;
  if (ast.acc_title && !ast.acc_title->empty()) model.acc_title = trim_start(*ast.acc_title);
  if (ast.acc_descr && !ast.acc_descr->empty()) {
    static const js_regex line_indent(R"(\n\s+)");
    model.acc_descr = line_indent.replace_all(*ast.acc_descr, ustring_view(u"\n"));
  }
  return model;
}

namespace {

struct quadrant {
  const char* name;
  int column;
  int row;
  int color;
};

// [domain, column, row, series color]
const quadrant kQuadrants[] = {{"complex", 0, 0, 0}, {"complicated", 1, 0, 1}, {"chaotic", 0, 1, 3}, {"clear", 1, 1, 2}};
constexpr int kConfusionColor = 4;

struct meta {
  const char16_t* name;
  const char16_t* model;
  const char16_t* practice;
};

// [name, decision model, kind of practice]. The arrow is U+2192.
const meta& meta_of(const std::string& domain) {
  static const meta complex_meta{u"Complex", u"Probe \u2192 Sense \u2192 Respond", u"Emergent Practices"};
  static const meta complicated_meta{u"Complicated", u"Sense \u2192 Analyse \u2192 Respond", u"Good Practices"};
  static const meta clear_meta{u"Clear", u"Sense \u2192 Categorise \u2192 Respond", u"Best Practices"};
  static const meta chaotic_meta{u"Chaotic", u"Act \u2192 Sense \u2192 Respond", u"Novel Practices"};
  static const meta confusion_meta{u"Confusion", u"", u"Disorder"};
  if (domain == "complex") return complex_meta;
  if (domain == "complicated") return complicated_meta;
  if (domain == "clear") return clear_meta;
  if (domain == "chaotic") return chaotic_meta;
  return confusion_meta;
}

constexpr std::size_t kMaxConfusionItems = 3;
constexpr double kItemMaxWidth = 200;
constexpr double kItemPadX = 10;
constexpr double kItemPadY = 4;
constexpr double kItemGap = 6;
constexpr double kHeadGap = 10;
constexpr double kMargin = 20;
constexpr double kClearance = 10;

struct badge {
  ustring text;
  label lab;
  double w = 0;
  double h = 0;
  bool overflow = false;
};

struct block {
  std::string name;
  label title;
  std::vector<label> subtitles;
  std::vector<badge> badges;
  double w = 0;
  double h = 0;
  double cx = 0;
  double cy = 0;
};

double setting(const config& settings, const char* key, double fallback, double min, double max) {
  const yaml_value* section = settings.find("cynefin");
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* v = section->find(key);
  return v != nullptr && v->is_number() && v->as_number() >= min && v->as_number() <= max ? v->as_number() : fallback;
}

// `x || 1`
double or_one(double x) { return x != 0 && !std::isnan(x) ? x : 1; }

}  // namespace

rendered render_cynefin(const cynefin_model& model, const config& settings, const render_options& options) {
  const double size = options.font_size ? *options.font_size : 16;
  const double small = js_round(size * 0.8125);
  const double item_size = js_round(size * 0.875);
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options.measurer != nullptr ? *options.measurer : default_measurer;
  const double pad = options.padding ? *options.padding : 8;
  const icon_set icons(options.icons);
  const label_layouter layouter(measurer, &icons);
  const yaml_value* conf = settings.find("cynefin");
  if (conf != nullptr && !conf->is_object()) conf = nullptr;
  const yaml_value* descriptions = conf != nullptr ? conf->find("showDomainDescriptions") : nullptr;
  const bool show_descriptions = !(descriptions != nullptr && descriptions->is_boolean() && !descriptions->as_boolean());
  const double amplitude = setting(settings, "boundaryAmplitude", 8, 0, 50);
  const double seed = resolve_seed(setting(settings, "seed", 0, -1e15, 1e15), to_utf16(options.id_prefix ? *options.id_prefix : std::string("pele")));
  const double item_line = js_round(item_size * 1.5);

  const auto make_badge = [&](const ustring& text, bool overflow) {
    badge b;
    b.text = text;
    b.lab = layouter.layout(text, false, item_size, kItemMaxWidth);
    b.w = b.lab.width + 2 * kItemPadX;
    b.h = std::max(b.lab.height, item_line) + 2 * kItemPadY;
    b.overflow = overflow;
    return b;
  };

  const auto make_block = [&](const std::string& name) {
    const meta& m = meta_of(name);
    block b;
    b.name = name;
    b.title = layouter.layout(m.name, false, size, 4000, text_style::bold);
    if (show_descriptions) {
      for (const char16_t* text : {m.model, m.practice})
        if (text[0] != u'\0') b.subtitles.push_back(layouter.layout(text, false, small, 4000, text_style::italic));
    }
    static const std::vector<ustring> none;
    const auto found = model.domains.find(name);
    const std::vector<ustring>& items = found != model.domains.end() ? found->second : none;
    const std::size_t shown = name == "confusion" && items.size() > kMaxConfusionItems ? kMaxConfusionItems : items.size();
    for (std::size_t i = 0; i < shown; i++) b.badges.push_back(make_badge(items[i], false));
    if (shown < items.size()) b.badges.push_back(make_badge(u"+" + to_utf16(js_number_string(static_cast<double>(items.size() - shown))) + u" more", true));
    double w = b.title.width;
    double h = b.title.height;
    for (const label& s : b.subtitles) {
      w = std::max(w, s.width);
      h += s.height;
    }
    if (!b.badges.empty()) h += kHeadGap - kItemGap;
    for (const badge& x : b.badges) {
      w = std::max(w, x.w);
      h += kItemGap + x.h;
    }
    b.w = w;
    b.h = h;
    return b;
  };

  std::vector<block> quadrants;
  for (const quadrant& q : kQuadrants) quadrants.push_back(make_block(q.name));
  block confusion = make_block("confusion");
  const double rx = std::max(88.0, confusion.w * 0.71 + 8);
  const double ry = std::max(64.0, confusion.h * 0.71 + 6);

  double quad_w = setting(settings, "width", 640, 100, 10000) / 2;
  double quad_h = setting(settings, "height", 440, 100, 10000) / 2;
  for (const block& b : quadrants) {
    quad_w = std::max(quad_w, b.w + 2 * kMargin);
    quad_h = std::max(quad_h, b.h + 2 * kMargin);
  }
  // A block sits in the middle of its quadrant, so its inner corner must stay clear of the ellipse.
  for (const block& b : quadrants) {
    const double dx = (quad_w - b.w) / 2;
    const double dy = (quad_h - b.h) / 2;
    const double ax = dx / (rx + kClearance);
    const double ay = dy / (ry + kClearance);
    if (ax * ax + ay * ay >= 1) continue;
    const double wider = b.w + 2 * (rx + kClearance);
    const double taller = b.h + 2 * (ry + kClearance);
    if (wider * quad_h <= quad_w * taller) quad_w = std::max(quad_w, wider);
    else quad_h = std::max(quad_h, taller);
  }

  const label title = layouter.layout(model.title ? ustring_view(*model.title) : ustring_view(), false, size, 4000, text_style::bold);
  const double title_height = title.height > 0 ? title.height + 12 : 0;
  const double width = 2 * quad_w;
  const double height = 2 * quad_h;
  const double x0 = pad;
  const double y0 = pad + title_height;
  const double cx = x0 + quad_w;
  const double cy = y0 + quad_h;
  const double right = x0 + width;
  const double bottom = y0 + height;

  wave_options fold_o = fold_options();
  fold_o.segments = 4;
  fold_o.pin_end = true;
  wave_options west_o = horizon_options();
  west_o.segments = 4;
  west_o.pin_end = true;
  wave_options east_o = horizon_options();
  east_o.segments = 4;
  east_o.pin_start = true;
  const std::vector<double> fold = wave(y0, cy, cx, seed, amplitude, fold_o);
  const std::vector<double> west = wave(x0, cx, cy, seed + 100, amplitude, west_o);
  const std::vector<double> east = wave(cx, right, cy, seed + 200, amplitude, east_o);
  const std::vector<double> drop = cliff(cx, cy, bottom, width * 0.03);
  const auto at = [](const std::vector<double>& curve, bool end) {
    return end ? svg::num(curve[curve.size() - 2]) + "," + svg::num(curve[curve.size() - 1]) : svg::num(curve[0]) + "," + svg::num(curve[1]);
  };

  const std::string complex_region = "M" + svg::num(x0) + "," + svg::num(y0) + "L" + at(fold, false) + forward(fold) + backward(west) + "Z";
  const std::string complicated_region = "M" + at(fold, false) + "L" + svg::num(right) + "," + svg::num(y0) + "L" + at(east, true) + backward(east) + backward(fold) + "Z";
  const std::string chaotic_region = "M" + at(west, false) + forward(west) + forward(drop) + "L" + svg::num(x0) + "," + svg::num(bottom) + "Z";
  const std::string clear_region = "M" + at(east, false) + forward(east) + "L" + svg::num(right) + "," + svg::num(bottom) + "L" + at(drop, true) + backward(drop) + "Z";
  const auto region = [&](const std::string& name) -> const std::string& {
    if (name == "complex") return complex_region;
    if (name == "complicated") return complicated_region;
    if (name == "chaotic") return chaotic_region;
    return clear_region;
  };

  std::string fills;
  for (const quadrant& q : kQuadrants)
    fills += std::string("<path class=\"pele-cynefin-region\" data-id=\"") + q.name + "\" d=\"" + region(q.name) + "\" fill=\"" + theme::series_color(q.color) + "\" fill-opacity=\"0.14\"/>";
  const std::string boundaries = "<g class=\"pele-cynefin-boundaries\" fill=\"none\" stroke=\"var(--_b)\" stroke-dasharray=\"6 3\">"
                                 "<path d=\"" + curve_path(fold) + "\"/><path d=\"" + curve_path(west) + "\"/><path d=\"" + curve_path(east) + "\"/>" +
                                 "<path class=\"pele-cynefin-cliff\" d=\"" + curve_path(drop) + "\" stroke=\"var(--_l)\" stroke-width=\"2\" stroke-dasharray=\"none\" stroke-linecap=\"round\"/></g>";
  const std::string oval = ellipse_path(cx, cy, rx, ry);
  const std::string ellipse = "<path d=\"" + oval + "\" fill=\"var(--_bg)\"/>" + "<path class=\"pele-cynefin-region\" data-id=\"confusion\" d=\"" + oval + "\" fill=\"" +
                              theme::series_color(kConfusionColor) + "\" fill-opacity=\"0.18\" stroke=\"var(--_b)\" stroke-dasharray=\"4 2\"/>";

  const auto draw_block = [&](const block& b) {
    double y = b.cy - b.h / 2;
    std::string out = svg::label_svg(b.title, b.cx, y + b.title.height / 2, " class=\"pele-label\" font-weight=\"var(--_hw)\"");
    y += b.title.height;
    for (const label& s : b.subtitles) {
      out += svg::label_svg(s, b.cx, y + s.height / 2, " class=\"pele-cynefin-subtitle\" font-size=\"" + js_number_string(small) + "\" font-style=\"italic\" fill=\"var(--_m)\"");
      y += s.height;
    }
    y += kHeadGap - kItemGap;
    for (const badge& item : b.badges) {
      y += kItemGap;
      out += std::string("<g class=\"pele-node") + (item.overflow ? " pele-cynefin-overflow" : "") + "\"" + (item.overflow ? std::string() : " data-id=\"" + svg::esc_text(item.text) + "\"") + ">" +
             "<rect x=\"" + svg::num(b.cx - item.w / 2) + "\" y=\"" + svg::num(y) + "\" width=\"" + svg::num(item.w) + "\" height=\"" + svg::num(item.h) + "\"" +
             " fill=\"var(--_bg)\" stroke=\"var(--_b)\"" + (item.overflow ? " stroke-dasharray=\"3 2\"" : "") + theme::kRounded + "/>" +
             svg::label_svg(item.lab, b.cx, y + item.h / 2, " class=\"pele-label\" font-size=\"" + js_number_string(item_size) + "\"" + (item.overflow ? " fill=\"var(--_m)\"" : ""), &icons) + "</g>";
      y += item.h;
    }
    return "<g class=\"pele-cynefin-domain\" data-id=\"" + b.name + "\">" + out + "</g>";
  };

  std::string domains;
  for (std::size_t index = 0; index < quadrants.size(); index++) {
    block& b = quadrants[index];
    b.cx = x0 + quad_w * (kQuadrants[index].column + 0.5);
    b.cy = y0 + quad_h * (kQuadrants[index].row + 0.5);
    domains += draw_block(b);
  }
  confusion.cx = cx;
  confusion.cy = cy;
  domains += draw_block(confusion);

  const auto find_block = [&](const std::string& name) -> const block* {
    if (name == "confusion") return &confusion;
    for (std::size_t i = 0; i < quadrants.size(); i++)
      if (name == kQuadrants[i].name) return &quadrants[i];
    return nullptr;
  };
  const auto inside = [&](const block* b, double x, double y) {
    if (b == &confusion) {
      const double ex = (x - cx) / (rx + 4);
      const double ey = (y - cy) / (ry + 4);
      return ex * ex + ey * ey <= 1;
    }
    return std::abs(x - b->cx) <= b->w / 2 + 8 && std::abs(y - b->cy) <= b->h / 2 + 8;
  };

  std::string edges;
  std::map<std::string, int> seen;
  for (const cynefin_transition& transition : model.transitions) {
    const block* from = find_block(transition.from);
    const block* to = find_block(transition.to);
    if (from == nullptr || to == nullptr || from == to) continue;
    const std::string key = transition.from < transition.to ? transition.from + " " + transition.to : transition.to + " " + transition.from;
    const int repeat = seen.count(key) ? seen[key] : 0;
    seen[key] = repeat + 1;

    const double dx = to->cx - from->cx;
    const double dy = to->cy - from->cy;
    const double len = or_one(js_hypot(dx, dy));
    const double mx = (from->cx + to->cx) / 2;
    const double my = (from->cy + to->cy) / 2;
    // Curves bow away from the middle of the diagram, which keeps them off the ellipse.
    const double outward = -dy * (mx - cx) + dx * (my - cy) >= 0 ? 1 : -1;
    const double nx = (-dy / len) * outward;
    const double ny = (dx / len) * outward;
    // A diagonal runs through the middle, so it bows far enough to pass the ellipse.
    const bool diagonal = from != &confusion && to != &confusion && std::abs(dx) > 1 && std::abs(dy) > 1;
    const double bow = (diagonal ? 2 * (js_hypot(rx * nx, ry * ny) + 16) : len * 0.15) + repeat * 56;
    const double px = mx + nx * bow;
    const double py = my + ny * bow;
    const auto point = [&](double t) {
      const double u = 1 - t;
      return std::pair<double, double>{u * u * from->cx + 2 * u * t * px + t * t * to->cx, u * u * from->cy + 2 * u * t * py + t * t * to->cy};
    };
    const auto crossing = [&](const block* b, double lo, double hi, bool leaving) {
      for (int i = 0; i < 16; i++) {
        const double mid = (lo + hi) / 2;
        const auto p = point(mid);
        if (inside(b, p.first, p.second) == leaving) lo = mid;
        else hi = mid;
      }
      return (lo + hi) / 2;
    };
    const int steps = 48;
    int i = 1;
    while (i <= steps && inside(from, point(static_cast<double>(i) / steps).first, point(static_cast<double>(i) / steps).second)) i++;
    if (i > steps) continue;
    const double t0 = crossing(from, static_cast<double>(i - 1) / steps, static_cast<double>(i) / steps, true);
    while (i <= steps && !inside(to, point(static_cast<double>(i) / steps).first, point(static_cast<double>(i) / steps).second)) i++;
    const double t1 = i > steps ? 1 : crossing(to, static_cast<double>(i - 1) / steps, static_cast<double>(i) / steps, false);

    const auto start = point(t0);
    const auto finish = point(t1);
    const double sx = start.first, sy = start.second;
    double ex = finish.first, ey = finish.second;
    const auto blossom = [&](double a, double b, double c) { return (1 - t0) * (1 - t1) * a + ((1 - t0) * t1 + t0 * (1 - t1)) * b + t0 * t1 * c; };
    const double qx = blossom(from->cx, px, to->cx);
    const double qy = blossom(from->cy, py, to->cy);
    const double el = or_one(js_hypot(ex - qx, ey - qy));
    const double ux = (ex - qx) / el;
    const double uy = (ey - qy) / el;
    const std::string head = edge_geometry::marker("arrow_point", ex, ey, ux, uy, "var(--_l)");
    ex -= ux * edge_geometry::marker_trim("arrow_point");
    ey -= uy * edge_geometry::marker_trim("arrow_point");

    std::string text;
    if (transition.label) {
      const label lab = layouter.layout(*transition.label, false, small, kItemMaxWidth);
      const auto mid = point((t0 + t1) / 2);
      const double away = (std::abs(nx) * lab.width) / 2 + (std::abs(ny) * lab.height) / 2 + 6;
      const double half = lab.width / 2 + 4;
      const double tx = std::min(std::max(mid.first + nx * away, x0 + half), std::max(right - half, x0 + half));
      text = svg::label_svg(lab, tx, mid.second + ny * away, " class=\"pele-edge-label\" font-size=\"" + js_number_string(small) + "\"", &icons);
    }
    edges += "<g class=\"pele-edge\" data-id=\"" + transition.from + "-" + transition.to + "\">" + "<path d=\"M" + svg::num(sx) + "," + svg::num(sy) + "Q" + svg::num(qx) + "," + svg::num(qy) +
             " " + svg::num(ex) + "," + svg::num(ey) + "\" fill=\"none\" stroke=\"var(--_l)\"/>" + head + text + "</g>";
  }

  const double total_width = std::ceil(std::max(width, title.width) + 2 * pad);
  const double total_height = std::ceil(height + title_height + 2 * pad);
  const std::string content = svg::label_svg(title, x0 + width / 2, pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") + "<g class=\"pele-cynefin-regions\">" + fills +
                              "</g>" + boundaries + ellipse + domains + (!edges.empty() ? "<g class=\"pele-edges\">" + edges + "</g>" : std::string());

  accessible acc;
  if (model.acc_title) acc.title = *model.acc_title;
  if (model.acc_descr) acc.description = *model.acc_descr;
  rendered result;
  result.svg = svg_document::build("cynefin", total_width, total_height, size, options, acc, content);
  result.width = total_width;
  result.height = total_height;
  return result;
}

}  // namespace pele::cynefin
