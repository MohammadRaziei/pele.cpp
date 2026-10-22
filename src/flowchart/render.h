#pragma once

#include "db.h"
#include "preprocess.h"
#include "render_options.h"
#include "rendered.h"

namespace pele::flowchart {

// Draws a parsed flowchart as an SVG document: lays it out, then writes every cluster, node and edge.
rendered render_flowchart(const flow_db& db, const config& settings, const render_options& options);

}  // namespace pele::flowchart
