#pragma once

#include "class/types.h"
#include "render_options.h"
#include "rendered.h"
#include "preprocess.h"

namespace pele::classdiagram {

rendered render_class(const class_model& model, const config& settings, const render_options& options);

}  // namespace pele::classdiagram
