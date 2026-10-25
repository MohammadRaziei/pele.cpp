#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::cynefin {

struct domain_block_ast {
  // complex, complicated, clear, chaotic or confusion
  std::string domain;
  std::vector<ustring> items;
};

struct transition_ast {
  std::string from;
  std::string to;
  std::optional<ustring> label;
};

struct cynefin_ast {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  std::vector<domain_block_ast> domains;
  std::vector<transition_ast> transitions;
};

cynefin_ast parse_cynefin(const ustring& source);

struct cynefin_transition {
  std::string from;
  std::string to;
  std::optional<ustring> label;
};

struct cynefin_model {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  // The domains the source declares, with their items. All five are drawn either way.
  std::map<std::string, std::vector<ustring>> domains;
  std::vector<cynefin_transition> transitions;
};

cynefin_model build_cynefin(const cynefin_ast& ast, const std::optional<ustring>& title);

rendered render_cynefin(const cynefin_model& model, const config& settings, const render_options& options);

}  // namespace pele::cynefin
