#include "packet/packet.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "common/fit_text.h"
#include "common/literal_label.h"
#include "common/tokens.h"
#include "entities.h"
#include "errors.h"
#include "fit_width.h"
#include "js_math.h"
#include "label.h"
#include "svg.h"
#include "svg_document.h"
#include "text_measurer.h"
#include "theme.h"

namespace pele::packet {
namespace {

// The order Pele numbers the tokens in.
enum token : int { T_packetBeta, T_packet, T_dash, T_plus, T_colon, T_accDescr, T_accTitle, T_title, T_int, T_string, T_newline };

const token_set& packet_tokens() {
  static const token_set set({
      token_def::literal("packet-beta", u"packet-beta"),
      common_tokens::keyword("packet"),
      // A front matter block also starts with dashes, so Langium lets it win over a dash.
      token_def::literal("-", u"-").longer({common_tokens::yaml()}),
      token_def::literal("+", u"+"),
      token_def::literal(":", u":"),
      common_tokens::acc_descr(),
      common_tokens::acc_title(),
      common_tokens::title(),
      token_def::regex("INT", R"(0|[1-9][0-9]*(?!\.))"),
      common_tokens::string(),
      common_tokens::newline(),
      common_tokens::whitespace(),
      common_tokens::yaml(),
      common_tokens::directive(),
      common_tokens::single_line_comment(),
  });
  return set;
}

// `Number(text)` for the digits of an INT token.
double number(const ustring& digits) { return std::strtod(to_utf8(digits).c_str(), nullptr); }

constexpr std::size_t kMaxRows = 10000;

pele_error invalid(const std::string& message) {
  error_info info;
  info.type = "packet";
  return pele_error(message, error_code::semantic, std::move(info));
}

// `(config.packet as Config | undefined)?.[key]` as a number in [min, max], or the fallback.
double packet_option(const config& settings, const char* key, double fallback, double min, double max) {
  const yaml_value* section = settings.find("packet");
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* value = section->find(key);
  return value != nullptr && value->is_number() && value->as_number() >= min ? std::min(value->as_number(), max) : fallback;
}

}  // namespace

packet_ast parse_packet(const ustring& src) {
  const token_set& types = packet_tokens();
  reader r(tokenize(src, types, "packet"), types, "packet");
  packet_ast ast;

  const auto end_of_line = [&]() {
    if (r.kind() == -1) return;
    if (r.kind() != T_newline) r.fail("a line break or the end of input");
    while (r.kind() == T_newline) r.i++;
  };

  while (r.kind() == T_newline) r.i++;
  if (!r.accept(T_packetBeta)) r.expect(T_packet);
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
      case T_int:
      case T_plus: {
        block_ast block;
        if (r.accept(T_plus)) {
          block.bits = number(r.expect(T_int));
        } else {
          block.start = number(r.take());
          if (r.accept(T_dash)) block.end = number(r.expect(T_int));
        }
        r.expect(T_colon);
        block.label = common_tokens::string_value(r.expect(T_string));
        ast.blocks.push_back(std::move(block));
        end_of_line();
        break;
      }
      default: r.fail("a field, a title, or a line break");
    }
  }
  return ast;
}

packet_model build_packet(const packet_ast& ast, const config& settings, const std::optional<ustring>& title) {
  packet_model model;
  const double bits_per_row = std::floor(packet_option(settings, "bitsPerRow", 32, 1, 4096));
  double last_bit = -1;
  std::vector<packet_block> word;
  double row = 1;

  for (const block_ast& block : ast.blocks) {
    std::optional<double> start = block.start, end = block.end, bits = block.bits;
    if (start && end && *end < *start)
      throw invalid("Packet block " + js_number_string(*start) + " - " + js_number_string(*end) + " is invalid. End must be greater than start.");
    if (!start) start = last_bit + 1;
    if (*start != last_bit + 1)
      throw invalid("Packet block " + js_number_string(*start) + " - " + js_number_string(end ? *end : *start) + " is not contiguous. It should start from " +
                    js_number_string(last_bit + 1) + ".");
    if (bits && *bits == 0) throw invalid("Packet block " + js_number_string(*start) + " is invalid. Cannot have a zero bit field.");
    if (!end) end = *start + (bits ? *bits : 1) - 1;
    if (!bits) bits = *end - *start + 1;
    last_bit = *end;

    double s = *start, b = *bits;
    while (word.size() <= static_cast<std::size_t>(bits_per_row) + 1 && model.rows.size() < kMaxRows) {
      const double row_end = row * bits_per_row - 1;
      if (*end <= row_end) {
        word.push_back({s, *end, b, block.label});
        if (*end == row_end) {
          model.rows.push_back(std::move(word));
          word.clear();
          row++;
        }
        break;
      }
      word.push_back({s, row_end, row_end - s, block.label});
      model.rows.push_back(std::move(word));
      word.clear();
      row++;
      s = row_end + 1;
      b = *end - s;
    }
  }
  if (!word.empty()) model.rows.push_back(std::move(word));

  // `ast.title || title`
  model.title = ast.title && !ast.title->empty() ? ast.title : title;
  if (ast.acc_title && !ast.acc_title->empty()) model.acc_title = trim_start(*ast.acc_title);
  if (ast.acc_descr && !ast.acc_descr->empty()) {
    static const js_regex line_indent(R"(\n\s+)");
    model.acc_descr = line_indent.replace_all(*ast.acc_descr, ustring_view(u"\n"));
  }
  model.bits_per_row = bits_per_row;
  return model;
}

namespace {

// A field of a row as drawn, and its label when the diagram runs down.
struct cell {
  packet_block block;
  std::optional<label> text;
};

rendered draw(const packet_model& model, const config& settings, const render_options& options, bool down) {
  const double size = options.font_size ? *options.font_size : 16;
  const double label_size = js_round(size * 0.875);
  const double bit_size = js_round(size * 0.625);
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options.measurer != nullptr ? *options.measurer : default_measurer;
  const label_layouter layouter(measurer);
  const double pad = options.padding ? *options.padding : 8;

  const yaml_value* section = settings.find("packet");
  if (section != nullptr && !section->is_object()) section = nullptr;
  const double row_height_option = packet_option(settings, "rowHeight", 32, 1, 400);
  double bit_width = packet_option(settings, "bitWidth", 32, 1, 400);
  const double padding_x = packet_option(settings, "paddingX", 5, 0, 400);
  const double padding_y = packet_option(settings, "paddingY", 5, 0, 400);
  bool show_bits = true;
  bool descending = false;
  if (section != nullptr) {
    const yaml_value* sb = section->find("showBits");
    show_bits = !(sb != nullptr && sb->is_boolean() && !sb->as_boolean());
    const yaml_value* order = section->find("bitOrder");
    descending = order != nullptr && order->is_string() && order->as_string() == u"descending";
  }
  double bits_per_row = model.bits_per_row;
  const double max_width = options.max_width ? *options.max_width : 320;

  std::vector<std::vector<packet_block>> blocks = model.rows;
  if (down) {
    const double count = std::max(1.0, std::floor((max_width - 2 * pad) / std::min(bit_width, 18.0)));
    // A byte is the shortest row. Below that every label is one character to a line, and a long
    // packet is a row to a bit.
    bits_per_row = std::min(bits_per_row, std::max(8.0, std::pow(2.0, std::floor(std::log2(count)))));
    double last = 0;
    if (!model.rows.empty() && !model.rows.back().empty()) last = model.rows.back().back().end;
    if (std::ceil((last + 1) / bits_per_row) > 10000) bits_per_row = model.bits_per_row;
    bit_width = std::min(bit_width, std::max(18.0, (max_width - 2 * pad) / bits_per_row));
    // The model has split each field at its own rows. Its pieces are joined again first, so that
    // a field is only split where one of the new rows ends. A piece counts one bit short.
    const auto piece = [](const packet_block& b) { return b.bits == b.end - b.start; };
    std::vector<packet_block> whole;
    for (const auto& row : model.rows) {
      for (const packet_block& block : row) {
        if (!whole.empty()) {
          packet_block& prev = whole.back();
          if (piece(prev) && piece(block) && prev.label == block.label && block.start == prev.end + 1 && std::fmod(block.start, model.bits_per_row) == 0) {
            prev.end = block.end;
            prev.bits = prev.end - prev.start;
            continue;
          }
        }
        whole.push_back(block);
      }
    }
    blocks.clear();
    for (const packet_block& block : whole) {
      for (double start = block.start; start <= block.end;) {
        const double end = std::min(block.end, (std::floor(start / bits_per_row) + 1) * bits_per_row - 1);
        const std::size_t index = static_cast<std::size_t>(std::floor(start / bits_per_row));
        if (blocks.size() <= index) blocks.resize(index + 1);
        packet_block piece_block = block;
        piece_block.start = start;
        piece_block.end = end;
        blocks[index].push_back(std::move(piece_block));
        start = end + 1;
      }
    }
  }

  std::vector<std::vector<cell>> rows;
  for (const auto& row : blocks) {
    std::vector<cell> cells;
    for (const packet_block& block : row) {
      cell c{block, std::nullopt};
      if (down) {
        const double room = std::max(1.0, (block.end - block.start + 1) * bit_width - std::min(padding_x, bit_width - 1) - 6);
        c.text = literal_label(trim(decode_entities(block.label)), measurer, label_size, room);
      }
      cells.push_back(std::move(c));
    }
    rows.push_back(std::move(cells));
  }

  // Bit numbers sit above each row and need room of their own.
  const double numbers = show_bits ? bit_size + 4 : 0;
  std::vector<double> row_heights;
  for (const auto& row : rows) {
    double height = row_height_option;
    if (down)
      for (const cell& c : row) height = std::max(height, c.text->height + 10);
    row_heights.push_back(height);
  }
  std::vector<double> tops;
  double rows_height = 0;
  for (double height : row_heights) {
    tops.push_back(rows_height);
    rows_height += numbers + height + padding_y;
  }
  if (!rows.empty()) rows_height -= padding_y;

  const double gap = std::min(padding_x, bit_width - 1);
  const double width = !rows.empty() ? bits_per_row * bit_width - gap : 0;
  const label title = layouter.layout(model.title ? ustring_view(*model.title) : ustring_view(), false, size, down ? fit_width::title_room(options) : 4000, text_style::bold);
  const double title_height = title.height > 0 ? title.height + (rows_height > 0 ? 12 : 0) : 0;

  // The pieces of a field split across rows share its label, so each width is fitted once.
  std::map<std::pair<ustring, double>, std::optional<fitted>> fits;
  const auto fit = [&](const ustring& text, double max) -> const std::optional<fitted>& {
    const auto key = std::make_pair(text, max);
    auto it = fits.find(key);
    if (it == fits.end()) it = fits.emplace(key, fit_text(trim(decode_entities(text)), max, label_size, js_round(size * 0.5), measurer)).first;
    return it->second;
  };

  const double left = pad + std::max(0.0, (title.width - width) / 2);
  std::string fields;
  std::string bits;
  for (std::size_t index = 0; index < rows.size(); index++) {
    const double y = pad + tops[index] + numbers;
    const double row_height = row_heights[index];
    for (const cell& c : rows[index]) {
      const packet_block& block = c.block;
      const double count = block.end - block.start + 1;
      const double first = std::fmod(block.start, bits_per_row);
      const double x = left + (descending ? bits_per_row - first - count : first) * bit_width;
      const double w = count * bit_width - gap;
      std::string inner;
      if (down) {
        inner = svg::label_svg(*c.text, x + w / 2, y + row_height / 2, "");
      } else {
        const std::optional<fitted>& text = fit(block.label, w - 6);
        if (text && text->size <= row_height)
          inner = "<text x=\"" + svg::num(x + w / 2) + "\" y=\"" + svg::num(y + row_height / 2 + text->size * 0.35) + "\"" +
                  (text->size == label_size ? std::string() : " font-size=\"" + svg::num(text->size) + "\"") + ">" + svg::esc(text->text) + "</text>";
      }
      fields += "<g class=\"pele-node\" data-id=\"" + js_number_string(block.start) + "-" + js_number_string(block.end) + "\">" + "<rect x=\"" + svg::num(x) + "\" y=\"" +
                svg::num(y) + "\" width=\"" + svg::num(w) + "\" height=\"" + svg::num(row_height) + "\" fill=\"var(--_s)\" stroke=\"var(--_b)\"" + theme::kRounded + "/>" +
                inner + "</g>";
      if (!show_bits) continue;
      const double leading = descending ? block.end : block.start;
      const double trailing = descending ? block.start : block.end;
      if (count == 1) {
        bits += "<text x=\"" + svg::num(x + w / 2) + "\" y=\"" + svg::num(y - 4) + "\" text-anchor=\"middle\">" + js_number_string(leading) + "</text>";
      } else {
        bits += "<text x=\"" + svg::num(x) + "\" y=\"" + svg::num(y - 4) + "\">" + js_number_string(leading) + "</text>" + "<text x=\"" + svg::num(x + w) + "\" y=\"" +
                svg::num(y - 4) + "\" text-anchor=\"end\">" + js_number_string(trailing) + "</text>";
      }
    }
  }

  const double total_width = std::ceil(std::max(width, title.width) + 2 * pad);
  const double total_height = std::ceil(rows_height + title_height + 2 * pad);
  const std::string content =
      (fields.empty() ? std::string() : "<g class=\"pele-packet-fields\" font-size=\"" + js_number_string(label_size) + "\" text-anchor=\"middle\">" + fields + "</g>") +
      (bits.empty() ? std::string() : "<g class=\"pele-packet-bits\" font-size=\"" + js_number_string(bit_size) + "\" fill=\"var(--_m)\">" + bits + "</g>") +
      // Mermaid puts a packet's title under it.
      svg::label_svg(title, total_width / 2, total_height - pad - title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"");

  accessible acc;
  if (model.acc_title) acc.title = *model.acc_title;
  if (model.acc_descr) acc.description = *model.acc_descr;
  rendered result;
  result.svg = svg_document::build("packet", total_width, total_height, size, options, acc, content);
  result.width = total_width;
  result.height = total_height;
  return result;
}

}  // namespace

rendered render_packet(const packet_model& model, const config& settings, const render_options& options) {
  return fit_width::turn_to_fit(options, [&](bool down) { return draw(model, settings, options, down); });
}

}  // namespace pele::packet
