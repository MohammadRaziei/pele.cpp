#pragma once

#include "class/db.h"
#include "ustring.h"

namespace pele::classdiagram {

// Reads the text of a class diagram into `db`. A syntax error is a pele_error worded as Mermaid words it.
void parse_class_diagram(const ustring& source, class_db& db);

}  // namespace pele::classdiagram
