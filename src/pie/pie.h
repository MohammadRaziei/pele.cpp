#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "common/tokens.h"
#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::pie {

struct pie_section {
  ustring label;
  double value = 0;
};

struct pie_ast {
  bool show_data = false;
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  std::vector<pie_section> sections;
};

// A scanner that answers as NUMBER_PIE's pattern does, without reading a whole run of digits
// again from every digit of it. Exposed for tests; the token table uses it.
scanner_fn number_scanner();

// The token table of a pie chart, which the tokenizer tests also read.
const token_set& pie_tokens();

pie_ast parse_pie(const ustring& source);

struct pie_model {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  bool show_data = false;
  // In source order; a label that comes twice keeps its first value.
  std::vector<pie_section> sections;
};

pie_model build_pie(const pie_ast& ast, const std::optional<ustring>& title);

rendered render_pie(const pie_model& model, const config& settings, const render_options& options);

}  // namespace pele::pie
