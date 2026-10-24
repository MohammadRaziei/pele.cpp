#pragma once

#include <optional>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::info {

// What an `info` diagram says.
struct info_ast {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
};

// Reads the text of an `info` diagram. A syntax error is a pele_error worded as Mermaid words it.
info_ast parse_info(const ustring& source);

struct info_model {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
};

info_model build_info(const info_ast& ast, const std::optional<ustring>& title);

rendered render_info(const info_model& model, const config& settings, const render_options& options);

}  // namespace pele::info
