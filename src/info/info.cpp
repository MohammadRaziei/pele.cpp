#include "info/info.h"

#include <algorithm>
#include <cmath>

#include "common/tokens.h"
#include "label.h"
#include "svg.h"
#include "svg_document.h"
#include "text_measurer.h"

namespace pele::info {
namespace {

// The order Pele numbers the tokens in.
enum token : int { T_showInfo, T_info, T_accDescr, T_accTitle, T_title, T_newline };

const token_set& info_tokens() {
  static const token_set set({
      common_tokens::keyword("showInfo"),
      common_tokens::keyword("info"),
      common_tokens::acc_descr(),
      common_tokens::acc_title(),
      common_tokens::title(),
      common_tokens::newline(),
      common_tokens::whitespace(),
      common_tokens::yaml(),
      common_tokens::directive(),
      common_tokens::single_line_comment(),
  });
  return set;
}

// Pele's version, which the diagram prints.
constexpr const char16_t* kVersion = u"1.12.0";

}  // namespace

info_ast parse_info(const ustring& src) {
  const token_set& types = info_tokens();
  reader r(tokenize(src, types, "info"), types, "info");
  info_ast ast;

  while (r.kind() == T_newline) r.i++;
  r.expect(T_info);
  while (r.kind() == T_newline) r.i++;
  if (r.accept(T_showInfo))
    while (r.kind() == T_newline) r.i++;
  while (r.kind() != -1) {
    const int kind = r.kind();
    if (kind == T_accDescr) ast.acc_descr = common_tokens::acc_descr_value(r.take());
    else if (kind == T_accTitle) ast.acc_title = common_tokens::acc_title_value(r.take());
    else if (kind == T_title) ast.title = common_tokens::title_value(r.take());
    else r.fail("a title or the end of input");
    if (r.kind() == -1) break;
    if (r.kind() != T_newline) r.fail("a line break or the end of input");
    while (r.kind() == T_newline) r.i++;
  }
  return ast;
}

info_model build_info(const info_ast& ast, const std::optional<ustring>& title) {
  info_model model;
  // `ast.title || title`
  model.title = ast.title && !ast.title->empty() ? ast.title : title;
  if (ast.acc_title && !ast.acc_title->empty()) model.acc_title = trim_start(*ast.acc_title);
  if (ast.acc_descr && !ast.acc_descr->empty()) {
    static const js_regex line_indent(R"(\n\s+)");
    model.acc_descr = line_indent.replace_all(*ast.acc_descr, ustring_view(u"\n"));
  }
  return model;
}

rendered render_info(const info_model& model, const config& /*settings*/, const render_options& options) {
  const double size = options.font_size ? *options.font_size : 16;
  const metrics_measurer default_measurer;
  const text_measurer& measurer = options.measurer != nullptr ? *options.measurer : default_measurer;
  const label_layouter layouter(measurer);
  const double pad = options.padding ? *options.padding : 8;

  const label title = layouter.layout(model.title ? ustring_view(*model.title) : ustring_view(), false, size, 4000, text_style::bold);
  const double title_height = title.height > 0 ? title.height + 8 : 0;
  const label text = layouter.layout(ustring(u"Pele v") + kVersion, false, size, 4000);

  const double width = std::ceil(std::max(title.width, text.width) + 2 * pad);
  const double height = std::ceil(title_height + text.height + 2 * pad);
  const std::string content = svg::label_svg(title, width / 2, pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") +
                              svg::label_svg(text, width / 2, pad + title_height + text.height / 2, " class=\"pele-version\" fill=\"var(--_m)\"");

  accessible acc;
  if (model.acc_title) acc.title = *model.acc_title;
  if (model.acc_descr) acc.description = *model.acc_descr;
  rendered result;
  result.svg = svg_document::build("info", width, height, size, options, acc, content);
  result.width = width;
  result.height = height;
  return result;
}

}  // namespace pele::info
