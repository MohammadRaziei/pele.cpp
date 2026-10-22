#pragma once

#include "db.h"

namespace pele::flowchart {

// Reads a flowchart into the db, by recursive descent over the tokens of the lexer. A syntax
// error is a pele_error worded as Mermaid words it.
void parse_flowchart(const ustring& source, flow_db& db);

}  // namespace pele::flowchart
