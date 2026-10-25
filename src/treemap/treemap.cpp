#include "treemap/treemap.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "common/fit_text.h"
#include "common/tokens.h"
#include "entities.h"
#include "fit_width.h"
#include "js_math.h"
#include "js_parse.h"
#include "label.h"
#include "svg.h"
#include "svg_document.h"
#include "text_measurer.h"
#include "theme.h"
#include "treemap/format.h"

namespace pele::treemap {
namespace {

// The order Pele numbers the tokens in.
enum token : int { T_accDescr, T_accTitle, T_title, T_keyword, T_classDef, T_styleSeparator, T_separator, T_comma, T_indentation, T_whitespace, T_comment, T_newline, T_id, T_number, T_string };

// Mermaid's treemap grammar has its own terminals. Every run of spaces or tabs is an INDENTATION
// token, since that comes before WS, and line breaks are hidden, so rows are told apart by their
// tokens alone and the indentation of a row is whatever whitespace precedes it.
const token_set& treemap_tokens() {
  static const token_set set({
      common_tokens::acc_descr(),
      common_tokens::acc_title(),
      common_tokens::title(),
      token_def::regex("TREEMAP_KEYWORD", R"(treemap-beta|treemap)"),
      token_def::regex("CLASS_DEF", R"(classDef\s+([a-zA-Z_][a-zA-Z0-9_]+)(?:\s+([^;\r\n]*))?(?:;)?)"),
      token_def::regex("STYLE_SEPARATOR", R"(:::)"),
      token_def::regex("SEPARATOR", R"(:)"),
      token_def::regex("COMMA", R"(,)"),
      token_def::regex("INDENTATION", R"([ \t]{1,})"),
      token_def::regex("WS", R"([ \t]+)").hidden(),
      token_def::regex("ML_COMMENT", R"(\%\%[^\n]*)").hidden(),
      token_def::regex("NL", R"(\r?\n)").hidden(),
      token_def::regex("ID2", R"([a-zA-Z_][a-zA-Z0-9_]*)"),
      token_def::regex("NUMBER2", R"([0-9_\.\,]+)"),
      token_def::regex("STRING2", R"("[^"]*"|'[^']*')"),
  });
  return set;
}

ustring replace_all_text(ustring s, const ustring& from, const ustring& to) {
  std::size_t at = 0;
  while ((at = s.find(from, at)) != ustring::npos) {
    s.replace(at, from.size(), to);
    at += to.size();
  }
  return s;
}

}  // namespace

treemap_ast parse_treemap(const ustring& src) {
  const token_set& types = treemap_tokens();
  reader r(tokenize(src, types, "treemap"), types, "treemap");
  treemap_ast ast;

  const auto class_selector = [&]() -> std::optional<ustring> {
    if (r.accept(T_styleSeparator)) return r.expect(T_id);
    return std::nullopt;
  };

  r.expect(T_keyword);
  while (r.kind() != -1) {
    const int kind = r.kind();
    if (kind == T_accDescr) {
      ast.acc_descr = common_tokens::acc_descr_value(r.take());
      continue;
    }
    if (kind == T_accTitle) {
      ast.acc_title = common_tokens::acc_title_value(r.take());
      continue;
    }
    if (kind == T_title) {
      ast.title = common_tokens::title_value(r.take());
      continue;
    }
    if (kind != T_indentation && kind != T_string && kind != T_classDef) r.fail("a node, a class definition, or a title");

    std::optional<int> indent;
    if (kind == T_indentation) indent = static_cast<int>(r.take().size());
    if (r.kind() == T_classDef) {
      static const js_regex re_class_def(R"(classDef\s+([A-Z_a-z]\w+)(?:\s+([^\n\r;]*))?;?)");
      const ustring text = r.take();
      const auto m = re_class_def.search(text);
      row_ast row;
      row.class_def = true;
      row.class_name = m->group(text, 1);
      if (m->has(2) && !m->group(text, 2).empty()) row.style_text = m->group(text, 2);
      row.indent = indent;
      ast.rows.push_back(std::move(row));
      continue;
    }

    row_ast row;
    const ustring quoted = r.expect(T_string);
    row.name = quoted.substr(1, quoted.size() - 2);
    // A leaf is a name followed by a colon or comma, with or without whitespace before it.
    const std::size_t after = r.kind_at(r.i) == T_indentation ? r.i + 1 : r.i;
    if (r.kind_at(after) == T_separator || r.kind_at(after) == T_comma) {
      r.i = after + 1;
      r.accept(T_indentation);
      row.leaf = true;
      const ustring number = r.expect(T_number);
      row.value = js_parse_float(replace_all_text(number, u",", u""));
    }
    row.class_selector = class_selector();
    row.indent = indent;
    ast.rows.push_back(std::move(row));
  }
  return ast;
}

treemap_model build_treemap(const treemap_ast& ast, const std::optional<ustring>& title) {
  std::map<ustring, std::vector<ustring>> classes;
  for (const row_ast& row : ast.rows) {
    if (!row.class_def) continue;
    std::vector<ustring>& styles = classes[row.class_name];
    // An escaped comma stays in the value; the others separate declarations.
    ustring text = row.style_text ? *row.style_text : ustring();
    text = replace_all_text(text, u"\\,", u"\u00A7\u00A7\u00A7");
    text = replace_all_text(text, u",", u";");
    text = replace_all_text(text, u"\u00A7\u00A7\u00A7", u",");
    std::size_t from = 0;
    for (;;) {
      const std::size_t semi = text.find(u';', from);
      styles.push_back(text.substr(from, semi == ustring::npos ? ustring::npos : semi - from));
      if (semi == ustring::npos) break;
      from = semi + 1;
    }
  }

  treemap_model model;
  // A row belongs to the nearest section above it that is indented less.
  struct open_section {
    int node;
    int level;
  };
  std::vector<open_section> stack;
  for (const row_ast& row : ast.rows) {
    if (row.class_def) continue;
    treemap_node node;
    node.name = row.name;
    node.section = !row.leaf;
    node.class_selector = row.class_selector;
    if (row.class_selector && !row.class_selector->empty()) {
      const auto found = classes.find(*row.class_selector);
      if (found != classes.end() && !found->second.empty()) node.compiled_styles = found->second;
    }
    if (row.leaf) node.value = row.value;
    const int level = row.indent ? *row.indent : 0;

    const int id = static_cast<int>(model.nodes.size());
    while (!stack.empty() && stack.back().level >= level) stack.pop_back();
    if (stack.empty()) model.roots.push_back(id);
    else model.nodes[static_cast<std::size_t>(stack.back().node)].children.push_back(id);
    if (node.section) stack.push_back({id, level});
    model.nodes.push_back(std::move(node));
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

struct box {
  int node = 0;
  int parent = -1;
  // Position among the top-level nodes of the branch this box belongs to, which picks its color.
  int series = 0;
  // A leaf's own value, or the sum of the leaves under a section.
  double value = 0;
  std::vector<int> children;
  double x = 0, y = 0, w = 0, h = 0;
};

struct insets {
  double gap;
  // Space a section keeps for its header, and around its children on the other three sides.
  double header;
  double side;
};

box make_box(const treemap_model& model, int node, int parent, int series) {
  const treemap_node& n = model.nodes[static_cast<std::size_t>(node)];
  box b;
  b.node = node;
  b.parent = parent;
  b.series = series;
  b.value = !n.section && n.value && std::isfinite(*n.value) && *n.value > 0 ? *n.value : 0;
  return b;
}

// Lays out boxes[from..to) in the rectangle as one squarified treemap: rows are added along the
// shorter side, and a row stops growing when another box would make its boxes less square.
void squarify(std::vector<box>& all, const std::vector<int>& boxes, double total, double x, double y, double w, double h, double gap) {
  double rest = total;
  std::size_t from = 0;
  while (from < boxes.size()) {
    const double scale = (w * h) / rest;
    const double side = std::min(w, h);
    double sum = all[static_cast<std::size_t>(boxes[from])].value * scale;
    const double largest = sum;
    double worst = std::max((side * side * largest) / (sum * sum), (sum * sum) / (side * side * largest));
    std::size_t to = from + 1;
    while (to < boxes.size()) {
      const double smallest = all[static_cast<std::size_t>(boxes[to])].value * scale;
      const double next = sum + smallest;
      const double ratio = std::max((side * side * largest) / (next * next), (next * next) / (side * side * smallest));
      if (ratio > worst) break;
      worst = ratio;
      sum = next;
      to++;
    }

    const double thickness = sum / side;
    double offset = 0;
    for (std::size_t i = from; i < to; i++) {
      box& b = all[static_cast<std::size_t>(boxes[i])];
      const double length = i == to - 1 ? side - offset : (b.value * scale) / thickness;
      const double bx = w >= h ? x : x + offset;
      const double by = w >= h ? y + offset : y;
      const double x0 = js_round(bx + gap / 2);
      const double y0 = js_round(by + gap / 2);
      b.x = x0;
      b.y = y0;
      b.w = js_round(bx + (w >= h ? thickness : length) - gap / 2) - x0;
      b.h = js_round(by + (w >= h ? length : thickness) - gap / 2) - y0;
      offset += length;
      rest -= b.value;
    }
    if (w >= h) {
      x += thickness;
      w -= thickness;
    } else {
      y += thickness;
      h -= thickness;
    }
    from = to;
  }
}

struct layout_result {
  std::vector<box> all;
  // The boxes that got a size, parents before their children.
  std::vector<int> placed;
};

layout_result layout_treemap(const treemap_model& model, double width, double height, const insets& in) {
  layout_result out;
  std::vector<box>& all = out.all;
  std::vector<int> top;
  for (std::size_t index = 0; index < model.roots.size(); index++) {
    top.push_back(static_cast<int>(all.size()));
    all.push_back(make_box(model, model.roots[index], -1, static_cast<int>(index)));
  }
  for (std::size_t i = 0; i < all.size(); i++) {
    const int parent = static_cast<int>(i);
    const std::vector<int> kids = model.nodes[static_cast<std::size_t>(all[i].node)].children;
    for (int node : kids) {
      const int child = static_cast<int>(all.size());
      all.push_back(make_box(model, node, parent, all[i].series));
      all[static_cast<std::size_t>(parent)].children.push_back(child);
    }
  }
  for (std::size_t i = all.size(); i-- > 0;)
    if (all[i].parent >= 0) all[static_cast<std::size_t>(all[i].parent)].value += all[i].value;

  const double gap = in.gap;
  const auto place = [&](const std::vector<int>& children, double x, double y, double w, double h) {
    if (w <= 0 || h <= 0) return;
    std::vector<int> shown;
    for (int c : children)
      if (all[static_cast<std::size_t>(c)].value > 0) shown.push_back(c);
    std::stable_sort(shown.begin(), shown.end(), [&](int a, int b) { return all[static_cast<std::size_t>(a)].value > all[static_cast<std::size_t>(b)].value; });
    double total = 0;
    for (int c : shown) total += all[static_cast<std::size_t>(c)].value;
    if (shown.empty() || !std::isfinite(total)) return;
    squarify(all, shown, total, x - gap / 2, y - gap / 2, w + gap, h + gap, gap);
    for (int c : shown)
      if (all[static_cast<std::size_t>(c)].w > 0 && all[static_cast<std::size_t>(c)].h > 0) out.placed.push_back(c);
  };

  place(top, 0, 0, width, height);
  for (std::size_t i = 0; i < out.placed.size(); i++) {
    const box b = all[static_cast<std::size_t>(out.placed[i])];
    if (!b.children.empty()) place(b.children, b.x + in.side, b.y + in.header, b.w - 2 * in.side, b.h - in.header - in.side);
  }
  return out;
}

constexpr double kHeader = 28;
constexpr double kSide = 6;
constexpr double kLine = 1.25;

double option(const config& settings, const char* key, double fallback, double min, double max) {
  const yaml_value* section = settings.find("treemap");
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* v = section->find(key);
  return v != nullptr && v->is_number() && v->as_number() >= min ? std::min(v->as_number(), max) : fallback;
}

}  // namespace

rendered render_treemap(const treemap_model& model, const config& settings, const render_options& options) {
  const double size = options.font_size ? *options.font_size : 16;
  const double small = js_round(size * 0.875);
  const double tiny = js_round(size * 0.75);
  const double least = js_round(size * 0.5625);
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options.measurer != nullptr ? *options.measurer : default_measurer;
  const label_layouter layouter(measurer);
  const yaml_value* conf = settings.find("treemap");
  if (conf != nullptr && !conf->is_object()) conf = nullptr;
  const double pad = options.padding ? *options.padding : option(settings, "diagramPadding", 8, 0, 2000);

  // Mermaid sizes the diagram as ten times `nodeWidth` by ten times `nodeHeight`.
  const double width = js_round(fit_width::cap_width(options, option(settings, "nodeWidth", 64, 1, 400) * 10, 200, 2 * pad));
  const double height = js_round(option(settings, "nodeHeight", 40, 1, 400) * 10);
  const yaml_value* show = conf != nullptr ? conf->find("showValues") : nullptr;
  const bool show_values = !(show != nullptr && show->is_boolean() && !show->as_boolean());
  const yaml_value* spec = conf != nullptr ? conf->find("valueFormat") : nullptr;
  const value_formatter format(spec != nullptr && spec->is_string() ? ustring_view(spec->as_string()) : ustring_view(u","));
  const layout_result laid = layout_treemap(model, width, height, {option(settings, "padding", 4, 0, 200), kHeader, kSide});

  const label title = layouter.layout(model.title ? ustring_view(*model.title) : ustring_view(), false, size, fit_width::title_room(options), text_style::bold);
  const double title_height = title.height > 0 ? title.height + 8 : 0;
  const double left = pad + std::max(0.0, (title.width - width) / 2);
  const double top = pad + title_height;

  static const js_regex fill_style(R"([";]fill:)");
  std::string sections;
  std::string leaves;
  for (int index : laid.placed) {
    const box& b = laid.all[static_cast<std::size_t>(index)];
    const treemap_node& node = model.nodes[static_cast<std::size_t>(b.node)];
    const theme::resolved_style style = theme::resolve_style(node.compiled_styles ? *node.compiled_styles : std::vector<ustring>());
    const std::string color = theme::series_color(b.series);
    const double x = left + b.x;
    const double y = top + b.y;
    const ustring name = trim(decode_entities(node.name));
    const std::string value = show_values ? format(b.value) : std::string();
    const std::string group = "data-id=\"" + svg::esc_text(node.name) + "\">";
    const std::string rect = "<rect x=\"" + svg::num(x) + "\" y=\"" + svg::num(y) + "\" width=\"" + svg::num(b.w) + "\" height=\"" + svg::num(b.h) + "\" rx=\"" + theme::kRadius +
                             "\" fill=\"" + color + "\"";
    const std::string classes = theme::class_names(node.class_selector ? ustring_view(*node.class_selector) : ustring_view());

    if (node.section) {
      std::string text;
      if (b.h >= kHeader) {
        const double room = b.w - 2 * kSide - 4;
        const double value_width = !value.empty() ? measurer.width(to_utf16(value), tiny, 0) : 0;
        const bool with_value = value_width > 0 && room - value_width - 12 >= 3 * small;
        const auto fitted_label = fit_text(name, with_value ? room - value_width - 12 : room, small, small, measurer, text_style::bold);
        const double baseline = y + kHeader / 2 + 1;
        if (fitted_label)
          text += "<text x=\"" + svg::num(x + kSide + 2) + "\" y=\"" + svg::num(baseline + small * 0.35) + "\" font-weight=\"var(--_hw)\"" + style.text + ">" + svg::esc(fitted_label->text) + "</text>";
        if (with_value)
          text += "<text class=\"pele-value\" x=\"" + svg::num(x + b.w - kSide - 2) + "\" y=\"" + svg::num(baseline + tiny * 0.35) + "\" font-size=\"" + js_number_string(tiny) +
                  "\" text-anchor=\"end\" fill=\"var(--_m)\"" + style.text + ">" + svg::esc(to_utf16(value)) + "</text>";
      }
      // A section is a tint of its color, unless a class gives it a fill of its own.
      const std::string tint = fill_style.search(to_utf16(style.shape)) ? std::string() : " fill-opacity=\"0.12\"";
      sections += "<g class=\"pele-node pele-section" + classes + "\" " + group + rect + tint + " stroke=\"" + color + "\"" + style.shape + "/>" + text + "</g>";
      continue;
    }

    std::string text;
    const double room = b.w - 8;
    const double tallest = std::min(size, std::floor((b.h - 4) / kLine));
    std::optional<fitted> label;
    if (tallest >= least) label = fit_text(name, room, tallest, least, measurer);
    if (label) {
      const std::string cx = svg::num(x + b.w / 2);
      const double cy = y + b.h / 2;
      const double value_size = std::max(least, js_round(label->size * 0.8));
      const double lines = (label->size + value_size) * kLine;
      if (!value.empty() && lines <= b.h - 4 && measurer.width(to_utf16(value), value_size, 0) <= room) {
        const double first = cy - lines / 2 + (label->size * kLine) / 2;
        const double second = cy + lines / 2 - (value_size * kLine) / 2;
        text = "<text x=\"" + cx + "\" y=\"" + svg::num(first + label->size * 0.35) + "\" font-size=\"" + svg::num(label->size) + "\"" + style.text + ">" + svg::esc(label->text) + "</text>" +
               "<text class=\"pele-value\" x=\"" + cx + "\" y=\"" + svg::num(second + value_size * 0.35) + "\" font-size=\"" + svg::num(value_size) + "\"" + style.text + ">" +
               svg::esc(to_utf16(value)) + "</text>";
      } else {
        text = "<text x=\"" + cx + "\" y=\"" + svg::num(cy + label->size * 0.35) + "\" font-size=\"" + svg::num(label->size) + "\"" + style.text + ">" + svg::esc(label->text) + "</text>";
      }
    }
    leaves += "<g class=\"pele-node pele-leaf" + classes + "\" " + group + rect + style.shape + "/>" + text + "</g>";
  }

  const double total_width = std::ceil(std::max(width, title.width) + 2 * pad);
  const double total_height = std::ceil(height + title_height + 2 * pad);
  const std::string content =
      svg::label_svg(title, total_width / 2, pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") +
      (!sections.empty() ? "<g class=\"pele-sections\" font-size=\"" + js_number_string(small) + "\">" + sections + "</g>" : std::string()) +
      (!leaves.empty() ? "<g class=\"pele-leaves\" text-anchor=\"middle\" fill=\"var(--_bg)\">" + leaves + "</g>" : std::string());

  accessible acc;
  if (model.acc_title) acc.title = *model.acc_title;
  if (model.acc_descr) acc.description = *model.acc_descr;
  rendered result;
  result.svg = svg_document::build("treemap", total_width, total_height, size, options, acc, content);
  result.width = total_width;
  result.height = total_height;
  return result;
}

}  // namespace pele::treemap
