#include "diagram.h"

#include "flowchart/db.h"
#include "flowchart/parser.h"
#include "flowchart/render.h"
#include "cynefin/cynefin.h"
#include "info/info.h"
#include "kanban/kanban.h"
#include "mindmap/mindmap.h"
#include "packet/packet.h"
#include "pie/pie.h"
#include "treemap/treemap.h"

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

class info_diagram_model final : public diagram_model {
 public:
  info::info_model model;
};

class info_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::info; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& /*settings*/, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& /*limits*/) const override {
    auto model = std::make_unique<info_diagram_model>();
    model->model = info::build_info(info::parse_info(source), title);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return info::render_info(static_cast<const info_diagram_model&>(model).model, settings, options);
  }
};

class packet_diagram_model final : public diagram_model {
 public:
  packet::packet_model model;
};

class packet_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::packet; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& settings, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& /*limits*/) const override {
    auto model = std::make_unique<packet_diagram_model>();
    model->model = packet::build_packet(packet::parse_packet(source), settings, title);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return packet::render_packet(static_cast<const packet_diagram_model&>(model).model, settings, options);
  }
};

class kanban_diagram_model final : public diagram_model {
 public:
  kanban::kanban_model model;
};

class kanban_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::kanban; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& /*settings*/, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& /*limits*/) const override {
    auto model = std::make_unique<kanban_diagram_model>();
    model->model = kanban::parse_kanban(source, title);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return kanban::render_kanban(static_cast<const kanban_diagram_model&>(model).model, settings, options);
  }
};

class mindmap_diagram_model final : public diagram_model {
 public:
  mindmap::mindmap_model model;
};

class mindmap_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::mindmap; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& /*settings*/, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& /*limits*/) const override {
    auto model = std::make_unique<mindmap_diagram_model>();
    model->model = mindmap::parse_mindmap(source, title);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return mindmap::render_mindmap(static_cast<const mindmap_diagram_model&>(model).model, settings, options);
  }
};

class pie_diagram_model final : public diagram_model {
 public:
  pie::pie_model model;
};

class pie_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::pie; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& /*settings*/, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& /*limits*/) const override {
    auto model = std::make_unique<pie_diagram_model>();
    model->model = pie::build_pie(pie::parse_pie(source), title);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return pie::render_pie(static_cast<const pie_diagram_model&>(model).model, settings, options);
  }
};

class treemap_diagram_model final : public diagram_model {
 public:
  treemap::treemap_model model;
};

class treemap_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::treemap; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& /*settings*/, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& /*limits*/) const override {
    auto model = std::make_unique<treemap_diagram_model>();
    model->model = treemap::build_treemap(treemap::parse_treemap(source), title);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return treemap::render_treemap(static_cast<const treemap_diagram_model&>(model).model, settings, options);
  }
};

class cynefin_diagram_model final : public diagram_model {
 public:
  cynefin::cynefin_model model;
};

class cynefin_diagram final : public diagram {
 public:
  diagram_type type() const override { return diagram_type::cynefin; }

  std::unique_ptr<diagram_model> parse(const ustring& source, const config& /*settings*/, const std::optional<ustring>& title, int /*line_offset*/,
                                       const parse_limits& /*limits*/) const override {
    auto model = std::make_unique<cynefin_diagram_model>();
    model->model = cynefin::build_cynefin(cynefin::parse_cynefin(source), title);
    return model;
  }

  rendered render(const diagram_model& model, const config& settings, const render_options& options) const override {
    return cynefin::render_cynefin(static_cast<const cynefin_diagram_model&>(model).model, settings, options);
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
    r.add(std::make_unique<info_diagram>());
    r.add(std::make_unique<packet_diagram>());
    r.add(std::make_unique<kanban_diagram>());
    r.add(std::make_unique<mindmap_diagram>());
    r.add(std::make_unique<pie_diagram>());
    r.add(std::make_unique<treemap_diagram>());
    r.add(std::make_unique<cynefin_diagram>());
    return r;
  }();
  return registry;
}

}  // namespace pele
