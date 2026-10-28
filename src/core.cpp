#include "pele.h"

#include "detect.h"
#include "diagram.h"
#include "entities.h"
#include "preprocess.h"

namespace pele {
namespace {

// Mermaid's default maxTextSize.
constexpr double kDefaultLimit = 50000;
// Mermaid has no limit on its output. A source within the limit can still describe a drawing of
// many megabytes, which would stall the page it is put into.
constexpr double kDefaultOutputLimit = 4000000;
// Mermaid stops at 500. Pele draws 5,000 edges in tens of milliseconds, and `A & B --> C & D`
// multiplies: without a limit, a few kilobytes of source ask for millions of edges.
constexpr double kDefaultMaxEdges = 5000;

pele_error limit_error(const std::string& message, const std::optional<detected>& found = std::nullopt) {
  error_info info;
  if (found) {
    info.type = to_string(found->type);
    info.mermaid_type = found->mermaid_type;
  }
  return pele_error(message, error_code::limit, std::move(info));
}

// The length of UTF-8 text in UTF-16 code units, which is what a JavaScript string counts.
std::size_t utf16_length(std::string_view s) {
  std::size_t n = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) n += c >= 0xF0 ? 2 : 1;
  }
  return n;
}

// Shows entity codes in an error the way they were written.
void show_written_codes(pele_error& error) {
  const std::string message = to_utf8(written_codes(to_utf16(error.what())));
  const std::string snippet = to_utf8(written_codes(to_utf16(error.snippet())));
  if (message != error.what() || snippet != error.snippet()) error.rewrite(message, snippet);
}

}  // namespace

struct parsed_diagram::impl {
  detected found;
  std::string type;
  std::string mermaid_type;
  const diagram* handler;
  std::unique_ptr<diagram_model> model;
  config settings;
};

namespace {

std::shared_ptr<const parsed_diagram::impl> parse_source(const ustring& text, double limit, double max_edges, const std::optional<yaml_value>& extra) {
  if (static_cast<double>(text.size()) > limit)
    throw limit_error("Diagram source is longer than the limit of " + std::to_string(static_cast<long long>(limit)) + " characters.");
  preprocessed pre = preprocess(text);
  const auto found = diagram_detector::detect_diagram(pre.text);
  if (!found) throw pele_error("No diagram type detected for the given text.", error_code::unsupported_diagram);
  error_info info;
  info.type = to_string(found->type);
  info.mermaid_type = found->mermaid_type;
  const diagram* handler = diagram_registry::builtin().find(found->type);
  if (handler == nullptr) throw pele_error(std::string("Diagram type \"") + to_string(found->type) + "\" is not supported yet.", error_code::unsupported_diagram, info);
  config settings = yaml_value::object();
  if (extra && extra->is_object())
    for (const ustring& key : extra->keys()) settings.set(key, *extra->find(key));
  for (const ustring& key : pre.settings.keys()) settings.set(key, *pre.settings.find(key));
  const bool keep_comments = handler->keep_comments();
  const ustring parsed = encode_entities(keep_comments ? pre.with_comments : pre.text) + u"\n";
  parse_limits limits;
  limits.max_edges = max_edges;
  std::unique_ptr<diagram_model> model;
  // Record the preprocessing edits only when a source position is needed.
  edit_steps steps;
  bool recorded = false;
  ustring original;
  locating_scope scope(parsed, [&](long long at) {
    if (!recorded) {
      recorded = true;
      const preprocessed again = preprocess(text, &steps);
      encode_entities(keep_comments ? again.with_comments : cleanup_comments(again.with_comments, &steps), &steps);
      // Match the line break appended for parsing.
      original = normalize_line_breaks(text) + u"\n";
    }
    return std::make_pair(original, input_offset(steps, at));
  });
  try {
    model = handler->parse(parsed, settings, pre.title, pre.line_offset, limits);
  } catch (pele_error& error) {
    error.tag(to_string(found->type), found->mermaid_type);
    show_written_codes(error);
    throw;
  }
  auto result = std::make_shared<parsed_diagram::impl>();
  result->found = *found;
  result->type = to_string(found->type);
  result->mermaid_type = found->mermaid_type;
  result->handler = handler;
  result->model = std::move(model);
  result->settings = std::move(settings);
  return result;
}

}  // namespace

const std::string& parsed_diagram::type() const { return impl_->type; }

const std::string& parsed_diagram::mermaid_type() const { return impl_->mermaid_type; }

parsed_diagram parse(std::string_view text, const render_options& options) {
  parsed_diagram result;
  result.impl_ = parse_source(to_utf16(text), options.limit ? *options.limit : kDefaultLimit, options.max_edges ? *options.max_edges : kDefaultMaxEdges, options.config);
  return result;
}

render_result render(const parsed_diagram& diagram, const render_options& options) {
  const parsed_diagram::impl& p = *diagram.impl_;
  const std::string& type = p.type;
  const yaml_value* section = p.settings.find(p.handler->section());
  bool fixed = false;
  if (section != nullptr && section->is_object()) {
    const yaml_value* use = section->find("useMaxWidth");
    fixed = use != nullptr && use->is_boolean() && !use->as_boolean();
  }
  render_options effective = options;
  effective.responsive = options.responsive ? *options.responsive : !fixed;
  // The host's icon resolver sees names as the author meant them, with entity codes turned into their characters.
  if (options.icons) {
    const icon_set::resolver original = options.icons;
    effective.icons = [original](const std::string& name) { return original(to_utf8(decode_entities(to_utf16(name)))); };
  }
  rendered drawn;
  try {
    drawn = p.handler->render(*p.model, p.settings, effective);
  } catch (pele_error& error) {
    error.tag(type, p.found.mermaid_type);
    show_written_codes(error);
    throw;
  }
  const double output_limit = options.output_limit ? *options.output_limit : kDefaultOutputLimit;
  if (static_cast<double>(utf16_length(drawn.svg)) > output_limit)
    throw limit_error("Diagram output is longer than the limit of " + std::to_string(static_cast<long long>(output_limit)) + " characters.", p.found);
  render_result result;
  result.type = type;
  result.mermaid_type = p.found.mermaid_type;
  result.svg = std::move(drawn.svg);
  result.width = drawn.width;
  result.height = drawn.height;
  result.links = std::move(drawn.links);
  for (rendered_link& link : result.links) link.id = decode_entities(link.id);
  return result;
}

render_result render(std::string_view text, const render_options& options) { return render(parse(text, options), options); }

std::optional<std::string> detect_type(std::string_view text) {
  const auto found = diagram_detector::detect_diagram(preprocess(to_utf16(text)).text);
  if (!found) return std::nullopt;
  return std::string(to_string(found->type));
}

bool supports(std::string_view text) {
  try {
    const auto found = diagram_detector::detect_diagram(preprocess(to_utf16(text)).text);
    return found && diagram_registry::builtin().find(found->type) != nullptr;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace pele
