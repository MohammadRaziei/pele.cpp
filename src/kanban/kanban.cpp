#include "kanban/kanban.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "common/code_points.h"
#include "common/icon.h"
#include "common/outline.h"
#include "errors.h"
#include "fit_width.h"
#include "js_math.h"
#include "label.h"
#include "lowercase.h"
#include "svg.h"
#include "svg_document.h"
#include "text_measurer.h"
#include "theme.h"
#include "url.h"
#include "yaml.h"

namespace pele::kanban {
namespace {

pele_error invalid(const ustring& message, error_code code) {
  error_info info;
  info.type = "kanban";
  return pele_error(to_utf8(message), code, std::move(info));
}

class kanban_db final : public outline::outline_db {
 public:
  kanban_model model;

  void add_node(int level, const ustring& id, const ustring& descr, int /*type*/, const std::optional<ustring>& shape_data) override {
    kanban_node node;
    node.id = id;
    node.level = level;
    node.label = descr;

    if (shape_data) {
      const ustring yaml_text = shape_data->find(u'\n') != ustring::npos ? *shape_data + u"\n" : u"{\n" + *shape_data + u"\n}";
      yaml_value doc;
      try {
        doc = parse_yaml(yaml_text);
      } catch (const yaml_error& error) {
        throw invalid(u"Metadata of \"" + node.id + u"\" is not valid YAML. " + to_utf16(error.what()), error_code::syntax);
      }
      // Mermaid throws a TypeError on metadata that is empty or not a mapping. Here it is ignored.
      if (doc.is_object()) {
        const yaml_value* shape_value = doc.find("shape");
        const ustring shape = shape_value != nullptr && shape_value->truthy() ? shape_value->to_js_string() : ustring();
        if (changes_when_lowercased(shape) || shape.find(u'_') != ustring::npos)
          throw invalid(u"No such shape: " + shape + u". Shape names should be lowercase.", error_code::semantic);
        assign(doc, "label", node.label);
        assign(doc, "icon", node.icon);
        assign(doc, "assigned", node.assigned);
        assign(doc, "ticket", node.ticket);
        assign(doc, "priority", node.priority);
      }
    }

    // A node indented less than the first column becomes a card, and Mermaid rejects whatever follows it.
    if (misplaced_)
      throw invalid(u"Items without section detected, found section (\"" + *misplaced_ + u"\")", error_code::semantic);
    auto& sections = model.sections;
    if (sections.empty()) section_level_ = level;
    if (level == section_level_) {
      kanban_section section;
      static_cast<kanban_node&>(section) = node;
      sections.push_back(std::move(section));
      last_section_ = sections.size() - 1;
      last_item_ = std::nullopt;
    } else {
      sections.back().items.push_back(node);
      last_section_ = sections.size() - 1;
      last_item_ = sections.back().items.size() - 1;
      if (level < section_level_) misplaced_ = node.label;
    }
  }

  // Mermaid throws a TypeError when a decoration comes before any node. Here it is ignored.
  void decorate_node(const std::optional<ustring>& icon, const std::optional<ustring>& css_class) override {
    if (!last_section_) return;
    kanban_section& section = model.sections[*last_section_];
    kanban_node& node = last_item_ ? section.items[*last_item_] : static_cast<kanban_node&>(section);
    if (icon && !icon->empty()) node.icon = *icon;
    if (css_class && !css_class->empty()) node.css_classes = *css_class;
  }

 private:
  // `if (doc.key) target = String(doc.key)`.
  static void assign(const yaml_value& doc, const char* key, ustring& target) {
    const yaml_value* v = doc.find(key);
    if (v != nullptr && v->truthy()) target = v->to_js_string();
  }
  static void assign(const yaml_value& doc, const char* key, std::optional<ustring>& target) {
    const yaml_value* v = doc.find(key);
    if (v != nullptr && v->truthy()) target = v->to_js_string();
  }

  int section_level_ = 0;
  std::optional<std::size_t> last_section_;
  std::optional<std::size_t> last_item_;
  std::optional<ustring> misplaced_;
};

constexpr double kColumnGap = 12;
constexpr double kColumnPad = 8;
constexpr double kCardPad = 10;
constexpr double kCardGap = 8;
constexpr double kMetaGap = 4;
constexpr double kIconGap = 6;

// Mermaid names five priorities and gives Medium no color. Matching here ignores case. The high
// ones are marked in the critical color and the low ones in the muted one, the nearer to Medium
// the fainter. They are not series colors, as a theme may put those in any order.
std::optional<std::string> priority_stroke(const ustring& priority) {
  if (priority == u"very high") return std::string("stroke=\"") + theme::kCritical + "\"";
  if (priority == u"high") return std::string("stroke=\"") + theme::kCritical + "\" opacity=\"0.5\"";
  if (priority == u"low") return std::string("stroke=\"var(--_m)\" opacity=\"0.5\"");
  if (priority == u"very low") return std::string("stroke=\"var(--_m)\"");
  return std::nullopt;
}

// ASCII letters folded, which is all the names above need: no other character lowercases to one of
// their letters.
ustring ascii_lower(ustring s) {
  for (char16_t& c : s)
    if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c + 32);
  return s;
}

// Lays out text for a fixed width. A word too long for a line is broken between characters,
// because a card cannot grow sideways to hold it.
label fit_label(const label_layouter& layouter, const text_measurer& measurer, ustring_view raw, bool markdown, double size, double max, int base = 0) {
  label laid = layouter.layout(raw, markdown, size, max, base);
  if (laid.width <= max) return laid;
  std::vector<std::vector<span>> lines;
  std::vector<double> widths;
  double widest = 0;
  std::vector<span> line;
  double width = 0;
  const auto commit = [&]() {
    lines.push_back(line);
    widths.push_back(width);
    widest = std::max(widest, width);
    line.clear();
    width = 0;
  };
  for (std::size_t i = 0; i < laid.lines.size(); i++) {
    if (laid.widths[i] <= max) {
      line = laid.lines[i];
      width = laid.widths[i];
      commit();
      continue;
    }
    for (const span& s : laid.lines[i]) {
      if (s.has_icon) {
        if (width > 0 && width + s.width > max) commit();
        line.push_back(s);
        width += s.width;
        continue;
      }
      ustring text;
      double from = width;
      for (const ustring& ch : code_points(s.text)) {
        const double w = measurer.width(ch, size, s.style);
        if (width > 0 && width + w > max) {
          if (!text.empty()) {
            span piece;
            piece.text = text;
            piece.style = s.style;
            piece.width = width - from;
            line.push_back(piece);
          }
          commit();
          text.clear();
          from = 0;
        }
        text += ch;
        width += w;
      }
      if (!text.empty()) {
        span piece;
        piece.text = text;
        piece.style = s.style;
        piece.width = width - from;
        line.push_back(piece);
      }
    }
    commit();
  }
  laid.lines = std::move(lines);
  laid.widths = std::move(widths);
  laid.width = widest;
  laid.height = static_cast<double>(laid.lines.size()) * laid.line_height;
  return laid;
}

// Writes a label with every line starting at x, or ending there, and the first line's box starting at y.
std::string text_block(const label& l, double x, double y, const std::string& attrs, const icon_set& icons, bool end = false) {
  std::string out;
  for (std::size_t i = 0; i < l.lines.size(); i++) {
    const auto& line = l.lines[i];
    const double cy = y + (static_cast<double>(i) + 0.5) * l.line_height;
    if (line.size() == 1 && !line[0].has_icon) {
      out += "<text" + svg::apply_shared_style(attrs, line[0].style) + " x=\"" + svg::num(x) + "\" y=\"" + svg::num(cy + l.size * 0.35) + "\"" +
             (end ? " text-anchor=\"end\"" : "") + ">" + svg::esc(line[0].text) + "</text>";
    } else {
      const double width = l.widths[i];
      label one = l;
      one.lines = {line};
      one.widths = {width};
      one.width = width;
      one.height = l.line_height;
      out += svg::label_svg(one, x + (end ? -width : width) / 2, cy, attrs, &icons);
    }
  }
  return out;
}

struct card {
  const kanban_node* item = nullptr;
  label text;
  label ticket;
  label assigned;
  // Height of the rows under the text that hold the ticket and the assignee.
  double meta = 0;
  double height = 0;
};

}  // namespace

kanban_model parse_kanban(const ustring& source, const std::optional<ustring>& title) {
  kanban_db db;
  db.model.title = title;
  outline::parse_outline(source, db, true);
  return std::move(db.model);
}

rendered render_kanban(const kanban_model& model, const config& settings, const render_options& options) {
  const double size = options.font_size ? *options.font_size : 16;
  const double small = js_round(size * 0.875);
  const double line = js_round(size * 1.5);
  const double small_line = js_round(small * 1.5);
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options.measurer != nullptr ? *options.measurer : default_measurer;
  const double pad = options.padding ? *options.padding : 8;
  const icon_set icons(options.icons);
  const label_layouter layouter(measurer, &icons);
  const auto has = [&](const std::optional<ustring>& name) { return name && !name->empty() && !icons.markup(to_utf8(*name)).empty(); };

  const yaml_value* conf = settings.find("kanban");
  if (conf != nullptr && !conf->is_object()) conf = nullptr;
  const yaml_value* section_width = conf != nullptr ? conf->find("sectionWidth") : nullptr;
  const double own_width = section_width != nullptr && section_width->is_number() && section_width->as_number() > 0 ? std::min(std::max(section_width->as_number(), 80.0), 2000.0) : 200;
  // A board wider than the width the host has is folded into rows of columns, which then
  // share the row between them.
  const double room = options.max_width && *options.max_width > 0 ? *options.max_width - 2 * pad : std::numeric_limits<double>::infinity();
  const double count = static_cast<double>(model.sections.size());
  const bool fits = count * (own_width + kColumnGap) - kColumnGap <= room;
  const double per_row = fits ? std::max(count, 1.0) : std::max(1.0, std::floor((room + kColumnGap) / (own_width + kColumnGap)));
  const double column_width =
      fits ? own_width : std::max(std::min(own_width, room), std::min(std::floor((room + kColumnGap) / per_row) - kColumnGap, own_width * 2));
  const yaml_value* base_url_value = conf != nullptr ? conf->find("ticketBaseUrl") : nullptr;
  const ustring base_url = base_url_value != nullptr && base_url_value->is_string() ? base_url_value->as_string() : ustring();
  const double card_width = column_width - 2 * kColumnPad;
  const double text_width = card_width - 2 * kCardPad;
  const double glyph = small * 1.25;
  std::vector<rendered_link> links;

  const auto& sections = model.sections;
  std::vector<label> titles;
  std::vector<double> count_widths;
  std::vector<std::vector<card>> cards;
  double head_height = line;
  std::vector<double> body_heights;
  for (const kanban_section& section : sections) {
    const double count_width = measurer.width(to_utf16(js_number_string(static_cast<double>(section.items.size()))), small, 0);
    const double reserved = count_width + 12 + (has(section.icon) ? glyph + kIconGap : 0);
    label title = fit_label(layouter, measurer, section.label, true, size, card_width - 8 - reserved, text_style::bold);
    head_height = std::max(head_height, title.height);
    count_widths.push_back(count_width);
    titles.push_back(std::move(title));

    std::vector<card> list;
    double height = 0;
    for (const kanban_node& item : section.items) {
      card c;
      c.item = &item;
      c.text = fit_label(layouter, measurer, item.label, true, small, text_width - (has(item.icon) ? glyph + kIconGap : 0));
      c.ticket = fit_label(layouter, measurer, item.ticket ? ustring_view(*item.ticket) : ustring_view(), false, small, text_width);
      c.assigned = fit_label(layouter, measurer, item.assigned ? ustring_view(*item.assigned) : ustring_view(), false, small, text_width);
      c.meta = c.ticket.width + c.assigned.width + 8 > text_width ? c.ticket.height + c.assigned.height : std::max(c.ticket.height, c.assigned.height);
      c.height = 2 * kCardPad + std::max(c.text.height, small_line) + (c.meta != 0 ? kMetaGap + c.meta : 0);
      height += c.height + kCardGap;
      list.push_back(std::move(c));
    }
    cards.push_back(std::move(list));
    body_heights.push_back(height);
  }
  head_height += 2 * kColumnPad;
  std::vector<double> row_heights;
  std::vector<double> row_tops;
  const std::size_t columns = static_cast<std::size_t>(per_row);
  {
    double y = 0;
    for (std::size_t first = 0; first < sections.size(); first += columns) {
      double tallest = kCardGap;
      for (std::size_t index = first; index < std::min(first + columns, sections.size()); index++) tallest = std::max(tallest, body_heights[index]);
      row_tops.push_back(y);
      row_heights.push_back(head_height + tallest);
      y += head_height + tallest + kColumnGap;
    }
  }
  const double board_height = !row_heights.empty() ? row_tops.back() + row_heights.back() : 0;

  const label title = layouter.layout(model.title ? ustring_view(*model.title) : ustring_view(), false, size, fit_width::title_room(options), text_style::bold);
  const double title_height = title.height > 0 ? title.height + 16 : 0;
  const double board_width = std::max(std::min(count, per_row) * (column_width + kColumnGap) - kColumnGap, 0.0);
  const double width = std::max(board_width, title.width);
  const double left = pad + (width - board_width) / 2;
  const double edge = column_width - kColumnPad - 4;

  std::string out;
  for (std::size_t index = 0; index < sections.size(); index++) {
    const kanban_section& section = sections[index];
    const std::size_t row = index / columns;
    std::string body = "<rect width=\"" + js_number_string(column_width) + "\" height=\"" + svg::num(row_heights[row]) + "\" rx=\"" + theme::kRadius + "\" fill=\"var(--_s)\"/>" +
                       text_block(titles[index], kColumnPad + 4, kColumnPad, " class=\"pele-cluster-label\" font-weight=\"var(--_hw)\"", icons) +
                       "<text class=\"pele-count\" x=\"" + js_number_string(edge) + "\" y=\"" + svg::num(kColumnPad + line / 2 + small * 0.35) +
                       "\" text-anchor=\"end\" font-size=\"" + js_number_string(small) + "\" fill=\"var(--_m)\">" + js_number_string(static_cast<double>(section.items.size())) + "</text>";
    if (has(section.icon)) body += icon_svg(*section.icon, edge - count_widths[index] - kIconGap - glyph, kColumnPad + (line - glyph) / 2, glyph, icons);

    double y = head_height;
    for (const card& c : cards[index]) {
      const kanban_node& item = *c.item;
      const ustring priority = ascii_lower(trim(item.priority ? ustring_view(*item.priority) : ustring_view()));
      const std::optional<std::string> color = priority_stroke(priority);
      std::string inner = "<rect width=\"" + js_number_string(card_width) + "\" height=\"" + svg::num(c.height) + "\" rx=\"" + theme::kRadius + "\" fill=\"var(--_bg)\" stroke=\"var(--_b)\"/>";
      if (color) inner += "<path class=\"pele-priority\" d=\"M2.5,5V" + svg::num(c.height - 5) + "\" fill=\"none\" " + *color + " stroke-width=\"3\" stroke-linecap=\"round\"/>";
      inner += text_block(c.text, kCardPad, kCardPad, " class=\"pele-label\"", icons);
      if (has(item.icon)) inner += icon_svg(*item.icon, card_width - kCardPad - glyph, kCardPad + (small_line - glyph) / 2, glyph, icons);
      const double bottom = c.height - kCardPad;
      if (item.ticket && !item.ticket->empty()) {
        const double top = bottom - c.meta;
        std::optional<ustring> href;
        if (!base_url.empty()) {
          // `baseUrl.replace('#TICKET#', () => ticket)`: the first one only, and the ticket as it is.
          ustring replaced = base_url;
          const std::size_t at = replaced.find(u"#TICKET#");
          if (at != ustring::npos) replaced.replace(at, 8, *item.ticket);
          href = link_url(safe_url(replaced), options);
        }
        if (!href) {
          inner += text_block(c.ticket, kCardPad, top, " class=\"pele-ticket\" fill=\"var(--_m)\"", icons);
        } else {
          inner += "<a href=\"" + svg::esc(*href) + "\" target=\"_blank\"" + rel_attr(options) + ">" +
                   text_block(c.ticket, kCardPad, top, " class=\"pele-ticket\" fill=\"var(--_c)\"", icons) + "</a>";
          links.push_back({item.id, *href, false});
        }
      }
      if (item.assigned && !item.assigned->empty())
        inner += text_block(c.assigned, card_width - kCardPad, bottom - c.assigned.height, " class=\"pele-assigned\" fill=\"var(--_m)\"", icons, true);
      std::string priority_class;
      if (color) {
        ustring dashed = priority;
        const std::size_t sp = dashed.find(u' ');
        if (sp != ustring::npos) dashed[sp] = u'-';
        priority_class = " pele-priority-" + to_utf8(dashed);
      }
      body += "<g class=\"pele-node pele-card" + priority_class + theme::class_names(item.css_classes ? ustring_view(*item.css_classes) : ustring_view()) + "\" data-id=\"" +
              svg::esc_text(item.id) + "\" transform=\"translate(" + js_number_string(kColumnPad) + "," + svg::num(y) + ")\" font-size=\"" + js_number_string(small) + "\">" + inner + "</g>";
      y += c.height + kCardGap;
    }

    out += "<g class=\"pele-cluster pele-column" + theme::class_names(section.css_classes ? ustring_view(*section.css_classes) : ustring_view()) + "\" data-id=\"" +
           svg::esc_text(section.id) + "\" transform=\"translate(" + svg::num(left + static_cast<double>(index % columns) * (column_width + kColumnGap)) + "," +
           svg::num(pad + title_height + row_tops[row]) + ")\">" + body + "</g>";
  }

  const double total_width = std::ceil(width + 2 * pad);
  const double total_height = std::ceil(board_height + title_height + 2 * pad);
  const std::string content = svg::label_svg(title, total_width / 2, pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") + out;

  rendered result;
  result.svg = svg_document::build("kanban", total_width, total_height, size, options, accessible{}, content);
  result.width = total_width;
  result.height = total_height;
  result.links = std::move(links);
  return result;
}

}  // namespace pele::kanban
