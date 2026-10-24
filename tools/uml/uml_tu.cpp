// The translation unit clang-uml reads. It only has to include every header whose classes belong
// in a diagram, and be listed in compile_commands.json (the `uml_tu` target in CMakeLists.txt).
// Nothing here is linked into the library.

#include <pele/pele.h>

#include "cluster.h"
#include "common/fit_text.h"
#include "common/literal_label.h"
#include "common/tokens.h"
#include "compound.h"
#include "detect.h"
#include "diagram.h"
#include "edges.h"
#include "fit_width.h"
#include "flowchart/db.h"
#include "flowchart/graph.h"
#include "flowchart/lexer.h"
#include "flowchart/model.h"
#include "flowchart/parser.h"
#include "flowchart/render.h"
#include "info/info.h"
#include "label.h"
#include "layered.h"
#include "packet/packet.h"
#include "preprocess.h"
#include "shapes.h"
#include "svg_document.h"
