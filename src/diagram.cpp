#include "diagram.h"

#include "flowchart/db.h"
#include "flowchart/parser.h"
#include "flowchart/render.h"

namespace pele {
namespace {

class flowchart_model final : public diagram_model {
 public:
  explicit flowchart_model(flowchart::flow_db_options options) : db(std::move(options)) {}
  flowchart::flow_db db;
};

class flowchart_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::flowchart; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& settings, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& limits) const override {
    flowchart::flow_db_options options;
    const yaml_value* flow = settings.find("flowchart");
    if (flow != nullptr && flow->is_object()) {
      const yaml_value* inherit = flow->find("inheritDir");
      options.inherit_dir = inherit != nullptr && inherit->is_boolean() && inherit->as_boolean();
    }
    options.max_edges = limits.max_edges;
    auto model = std::make_unique<flowchart_model>(std::move(options));
    if (title && !title->empty()) model->db.title = *title;
    flowchart::parse_flowchart(source, model->db);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return flowchart::render_flowchart(static_cast<const flowchart_model&>(model).db, settings, options);
  }
};

}  // namespace

void diagram_registry::add(std::unique_ptr<diagram> d) {
  const diagram_type type = d->type();
  diagrams_[type] = std::move(d);
}

const diagram* diagram_registry::find(diagram_type type) const {
  const auto it = diagrams_.find(type);
  return it == diagrams_.end() ? nullptr : it->second.get();
}

const diagram_registry& diagram_registry::builtin() {
  static const diagram_registry registry = [] {
    diagram_registry r;
    r.add(std::make_unique<flowchart_diagram>());
    return r;
  }();
  return registry;
}

}  // namespace pele
