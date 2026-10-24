#pragma once

#include <optional>
#include <vector>

#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele::packet {

// One field as written: `0-15: "Source Port"`, `16: "Flag"` or `+8: "Length"`.
struct block_ast {
  std::optional<double> start;
  std::optional<double> end;
  std::optional<double> bits;
  ustring label;
};

struct packet_ast {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  std::vector<block_ast> blocks;
};

// Reads the text of a packet diagram. A syntax error is a pele_error worded as Mermaid words it.
packet_ast parse_packet(const ustring& source);

struct packet_block {
  double start = 0;
  double end = 0;
  // As Mermaid counts it: one less than the width for the pieces of a field split across rows.
  double bits = 0;
  ustring label;
};

struct packet_model {
  std::optional<ustring> title;
  std::optional<ustring> acc_title;
  std::optional<ustring> acc_descr;
  double bits_per_row = 32;
  std::vector<std::vector<packet_block>> rows;
};

// Lays the fields out in rows; a field that does not fit the rest of a row is split.
packet_model build_packet(const packet_ast& ast, const config& settings, const std::optional<ustring>& title);

rendered render_packet(const packet_model& model, const config& settings, const render_options& options);

}  // namespace pele::packet
