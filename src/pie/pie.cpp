#include "pie/pie.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "errors.h"
#include "fit_width.h"
#include "js_math.h"
#include "label.h"
#include "svg.h"
#include "svg_document.h"
#include "text_measurer.h"
#include "theme.h"

namespace pele::pie {
namespace {

// The order Pele numbers the tokens in.
enum token : int { T_showData, T_pie, T_colon, T_number, T_accDescr, T_accTitle, T_title, T_string, T_newline };

std::ptrdiff_t digits(ustring_view src, std::size_t from) {
  std::size_t i = from;
  while (i < src.size() && src[i] >= u'0' && src[i] <= u'9') i++;
  return static_cast<std::ptrdiff_t>(i);
}

int code_at(ustring_view src, std::ptrdiff_t i) { return i >= 0 && static_cast<std::size_t>(i) < src.size() ? src[static_cast<std::size_t>(i)] : -1; }

}  // namespace

// Answers as NUMBER_PIE's pattern does. The pattern reads a whole run of digits before it finds
// there is no fraction, and would do so again from every digit of a long run of zeros.
scanner_fn number_scanner() {
  std::ptrdiff_t from = 0;
  std::ptrdiff_t end = 0;
  std::ptrdiff_t fraction = -1;
  return [from, end, fraction](ustring_view src, std::size_t p) mutable -> std::ptrdiff_t {
    const std::ptrdiff_t at = code_at(src, static_cast<std::ptrdiff_t>(p)) == 45 ? static_cast<std::ptrdiff_t>(p) + 1 : static_cast<std::ptrdiff_t>(p);
    if (at < from || at >= end) {
      from = at;
      end = digits(src, static_cast<std::size_t>(at));
      fraction = -1;
      if (end > at && code_at(src, end) == 46) {
        const std::ptrdiff_t stop = digits(src, static_cast<std::size_t>(end) + 1);
        const std::ptrdiff_t length = stop - end - 1;
        if (length > 0 && code_at(src, stop) != 46) fraction = stop;
        else if (length > 1) fraction = stop - 1;
      }
    }
    if (end == at) return -1;
    if (fraction >= 0) return fraction;
    if (code_at(src, at) == 48) return code_at(src, at + 1) == 46 ? -1 : at + 1;
    if (code_at(src, end) != 46) return end;
    return end - 1 > at ? end - 1 : -1;
  };
}

const token_set& pie_tokens() {
  static const token_set set({
      common_tokens::keyword("showData"),
      common_tokens::keyword("pie"),
      token_def::literal(":", u":"),
      token_def::regex("NUMBER_PIE", R"((?:-?[0-9]+\.[0-9]+(?!\.))|(?:-?(0|[1-9][0-9]*)(?!\.)))", "-0123456789").scanner([]() { return number_scanner(); }),
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

pie_ast parse_pie(const ustring& src) {
  const token_set& types = pie_tokens();
  reader r(tokenize(src, types, "pie"), types, "pie");
  pie_ast ast;

  const auto end_of_line = [&]() {
    if (r.kind() == -1) return;
    if (r.kind() != T_newline) r.fail("a line break or the end of input");
    while (r.kind() == T_newline) r.i++;
  };

  while (r.kind() == T_newline) r.i++;
  r.expect(T_pie);
  ast.show_data = r.accept(T_showData);
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
      case T_string: {
        pie_section section;
        section.label = common_tokens::string_value(r.take());
        r.expect(T_colon);
        section.value = std::strtod(to_utf8(r.expect(T_number)).c_str(), nullptr);
        ast.sections.push_back(std::move(section));
        end_of_line();
        break;
      }
      default: r.fail("a section, a title, or a line break");
    }
  }
  return ast;
}

pie_model build_pie(const pie_ast& ast, const std::optional<ustring>& title) {
  pie_model model;
  for (const pie_section& section : ast.sections) {
    if (section.value < 0) {
      error_info info;
      info.type = "pie";
      throw pele_error("\"" + to_utf8(section.label) + "\" has invalid value: " + js_number_string(section.value) +
                           ". Negative values are not allowed in pie charts. All slice values must be >= 0.",
                       error_code::semantic, std::move(info));
    }
    const bool seen = std::any_of(model.sections.begin(), model.sections.end(), [&](const pie_section& s) { return s.label == section.label; });
    if (!seen) model.sections.push_back(section);
  }
  // `ast.title || title`
  model.title = ast.title && !ast.title->empty() ? ast.title : title;
  if (ast.acc_title && !ast.acc_title->empty()) model.acc_title = trim_start(*ast.acc_title);
  if (ast.acc_descr && !ast.acc_descr->empty()) {
    static const js_regex line_indent(R"(\n\s+)");
    model.acc_descr = line_indent.replace_all(*ast.acc_descr, ustring_view(u"\n"));
  }
  model.show_data = ast.show_data;
  return model;
}

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadius = 120;
constexpr double kSwatch = 12;
constexpr double kRow = 26;
constexpr double kGap = 32;
constexpr double kOutside = 28;

std::string point(double angle, double radius) { return svg::num(std::sin(angle) * radius) + "," + svg::num(-std::cos(angle) * radius); }

std::string slice(double start, double end, double outer, double inner) {
  if (end - start >= kPi * 2 - 1e-9) {
    const auto ring = [](double r, int sweep) {
      const std::string s = std::to_string(sweep);
      return "M0," + svg::num(-r) + "A" + svg::num(r) + "," + svg::num(r) + " 0 1 " + s + " 0," + svg::num(r) + "A" + svg::num(r) + "," + svg::num(r) + " 0 1 " + s + " 0," +
             svg::num(-r) + "Z";
    };
    return ring(outer, 1) + (inner > 0 ? ring(inner, 0) : std::string());
  }
  const std::string large = end - start > kPi ? "1" : "0";
  if (inner <= 0) return "M0,0L" + point(start, outer) + "A" + svg::num(outer) + "," + svg::num(outer) + " 0 " + large + " 1 " + point(end, outer) + "Z";
  return "M" + point(start, outer) + "A" + svg::num(outer) + "," + svg::num(outer) + " 0 " + large + " 1 " + point(end, outer) + "L" + point(end, inner) + "A" + svg::num(inner) +
         "," + svg::num(inner) + " 0 " + large + " 0 " + point(start, inner) + "Z";
}

// A pie option as a number, when it is one.
std::optional<double> option_number(const config& settings, const char* key) {
  const yaml_value* section = settings.find("pie");
  if (section == nullptr || !section->is_object()) return std::nullopt;
  const yaml_value* v = section->find(key);
  if (v != nullptr && v->is_number()) return v->as_number();
  return std::nullopt;
}

// `String(option ?? 'right')`
ustring option_text(const config& settings, const char* key, const ustring& fallback) {
  const yaml_value* section = settings.find("pie");
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* v = section->find(key);
  if (v == nullptr || v->is_null()) return fallback;
  return v->to_js_string();
}

struct outside_label {
  double x;
  double y;
  double width;
};

// `tight` is set when the chart does not fit the width it has with its legend beside it.
rendered draw(const pie_model& model, const config& settings, const render_options& options, double radius, bool tight) {
  const double size = options.font_size ? *options.font_size : 16;
  const double small = js_round(size * 0.875);
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options.measurer != nullptr ? *options.measurer : default_measurer;
  const label_layouter layouter(measurer);
  const double pad = options.padding ? *options.padding : 8;

  const std::optional<double> position = option_number(settings, "textPosition");
  const double text_position = position && *position >= 0 && *position <= 1 ? *position : 0.75;
  const std::optional<double> hole = option_number(settings, "donutHole");
  const double inner = hole && *hole > 0 && *hole <= 0.9 ? *hole * radius : 0;
  const ustring placed = option_text(settings, "legendPosition", u"right");
  const ustring legend_position = tight && (placed == u"left" || placed == u"right") ? ustring(u"bottom") : placed;

  const auto& entries = model.sections;
  double sum = 0;
  for (const pie_section& e : entries) sum += e.value;

  std::vector<label> legend;
  for (const pie_section& e : entries)
    legend.push_back(layouter.layout(model.show_data ? e.label + u" [" + to_utf16(js_number_string(e.value)) + u"]" : e.label, false, small, 4000));
  double legend_width = 0;
  for (const label& l : legend) legend_width = std::max(legend_width, l.width);
  legend_width += !legend.empty() ? kSwatch + 8 : 0;
  const double legend_height = static_cast<double>(legend.size()) * kRow;

  const label title = layouter.layout(model.title ? ustring_view(*model.title) : ustring_view(), false, size, fit_width::title_room(options), text_style::bold);
  const double title_height = title.height > 0 ? title.height + 16 : 0;

  // A slice too thin for its percentage gets the label outside the rim, which needs a margin.
  std::vector<double> shares;
  for (const pie_section& e : entries) {
    const double share = sum > 0 ? e.value / sum : 0;
    shares.push_back(share * 100 >= 1 && js_to_fixed0(share * 100) != "0" ? share : 0);
  }
  std::vector<char> narrow;
  bool any_narrow = false;
  for (double share : shares) {
    bool is_narrow = false;
    if (!(share == 0 || share >= 0.5)) {
      const double chord = 2 * radius * text_position * std::sin(share * kPi);
      is_narrow = chord < measurer.width(to_utf16(js_to_fixed0(share * 100) + "%"), small, 0) + 6;
    }
    narrow.push_back(is_narrow ? 1 : 0);
    any_narrow = any_narrow || is_narrow;
  }
  const double margin = any_narrow ? kOutside : 0;

  const bool side_left = legend_position == u"left", side_right = legend_position == u"right", side_center = legend_position == u"center";
  const bool stacked = legend_position == u"top" || legend_position == u"bottom";
  const double diameter = (radius + margin) * 2;
  double width = diameter;
  double height = diameter;
  if (side_left || side_right) {
    width += legend_width > 0 ? kGap + legend_width : 0;
    height = std::max(height, legend_height);
  } else if (stacked) {
    width = std::max(width, legend_width);
    height += legend_height > 0 ? 20 + legend_height : 0;
  }
  width = std::max(width, title.width);

  const double cx = pad + (side_left ? legend_width + kGap + diameter / 2 : side_right ? diameter / 2 : width / 2);
  const double body_top = pad + title_height;
  const double cy = body_top + (legend_position == u"top" ? legend_height + 20 + diameter / 2 : stacked ? diameter / 2 : height / 2);

  std::string slices;
  std::string labels;
  std::string outside;
  double angle = 0;
  const double rim = radius + kOutside / 2;
  std::vector<outside_label> placed_outside;
  const auto clear = [&](double x, double y, double w) {
    return std::all_of(placed_outside.begin(), placed_outside.end(),
                       [&](const outside_label& other) { return std::abs(x - other.x) >= (w + other.width) / 2 + 2 || std::abs(y - other.y) >= small + 2; });
  };
  for (std::size_t index = 0; index < entries.size(); index++) {
    const double share = shares[index];
    if (share == 0) continue;
    const double end = angle + share * kPi * 2;
    slices += "<path class=\"pele-slice\" data-id=\"" + svg::esc_text(entries[index].label) + "\" d=\"" + slice(angle, end, radius, inner) + "\" fill=\"" + theme::series_color(static_cast<int>(index)) + "\"/>";
    const double mid = (angle + end) / 2;
    const std::string text = js_to_fixed0(share * 100) + "%";
    if (narrow[index]) {
      const double w = measurer.width(to_utf16(text), small, 0);
      double at = mid;
      for (int step = 0; step < 360 && !clear(std::sin(at) * rim, -std::cos(at) * rim, w); step++) at += 2 / rim;
      const double x = std::sin(at) * rim;
      const double y = -std::cos(at) * rim;
      placed_outside.push_back({x, y, w});
      outside += "<text x=\"" + svg::num(x) + "\" y=\"" + svg::num(y + small * 0.35) + "\">" + text + "</text>";
    } else {
      const double r = radius * text_position;
      labels += "<text x=\"" + svg::num(std::sin(mid) * r) + "\" y=\"" + svg::num(-std::cos(mid) * r + small * 0.35) + "\">" + text + "</text>";
    }
    angle = end;
  }
  if (slices.empty()) slices = "<circle class=\"pele-slice\" r=\"" + js_number_string(radius) + "\" fill=\"none\" stroke=\"var(--_b)\"/>";

  std::string legend_out;
  double lx, ly;
  if (side_right) {
    lx = cx + diameter / 2 + kGap;
    ly = body_top + (height - legend_height) / 2;
  } else if (side_left) {
    lx = pad;
    ly = body_top + (height - legend_height) / 2;
  } else if (side_center) {
    lx = cx - legend_width / 2;
    ly = cy - legend_height / 2;
  } else {
    lx = pad + (width - legend_width) / 2;
    ly = legend_position == u"top" ? body_top : body_top + diameter + 20;
  }
  for (std::size_t index = 0; index < legend.size(); index++) {
    const double y = ly + static_cast<double>(index) * kRow + kRow / 2;
    legend_out += "<g class=\"pele-legend-item\" data-id=\"" + svg::esc_text(entries[index].label) + "\">" + "<rect x=\"" + svg::num(lx) + "\" y=\"" + svg::num(y - kSwatch / 2) +
                  "\" width=\"" + js_number_string(kSwatch) + "\" height=\"" + js_number_string(kSwatch) + "\" rx=\"2\" fill=\"" + theme::series_color(static_cast<int>(index)) + "\"/>" +
                  svg::label_svg(legend[index], lx + kSwatch + 8 + legend[index].width / 2, y, "") + "</g>";
  }

  const double total_width = std::ceil(width + 2 * pad);
  const double total_height = std::ceil(height + title_height + 2 * pad);
  const std::string at_center = "translate(" + svg::num(cx) + "," + svg::num(cy) + ")";
  const std::string content =
      svg::label_svg(title, total_width / 2, pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") +
      "<g class=\"pele-slices\" transform=\"" + at_center + "\" stroke=\"var(--_bg)\" stroke-width=\"1\" stroke-linejoin=\"round\">" + slices + "</g>" +
      (!labels.empty() ? "<g class=\"pele-slice-labels\" transform=\"" + at_center + "\" font-size=\"" + js_number_string(small) + "\" text-anchor=\"middle\" fill=\"var(--_bg)\">" + labels + "</g>" : std::string()) +
      (!outside.empty() ? "<g class=\"pele-slice-labels\" transform=\"" + at_center + "\" font-size=\"" + js_number_string(small) + "\" text-anchor=\"middle\">" + outside + "</g>" : std::string()) +
      (!legend_out.empty() ? "<g class=\"pele-legend\" font-size=\"" + js_number_string(small) + "\">" + legend_out + "</g>" : std::string());

  accessible acc;
  if (model.acc_title) acc.title = *model.acc_title;
  if (model.acc_descr) acc.description = *model.acc_descr;
  rendered result;
  result.svg = svg_document::build("pie", total_width, total_height, size, options, acc, content);
  result.width = total_width;
  result.height = total_height;
  return result;
}

}  // namespace

rendered render_pie(const pie_model& model, const config& settings, const render_options& options) {
  return fit_width::fit_plot(options, kRadius * 2, 140, [&](double diameter, bool tight) { return draw(model, settings, options, diameter / 2, tight); });
}

}  // namespace pele::pie
