#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "render_options.h"
#include "rendered.h"
#include "ustring.h"

namespace pele {

// Follows the algorithm of @braintree/sanitize-url, which Mermaid applies to click targets.
// Gives `about:blank` for what it refuses.
ustring sanitize_url(const ustring& url);

// A URL as written in a diagram: entity codes are read first, so they cannot hide a scheme.
ustring safe_url(const ustring& url);

// Both take a URL that sanitize_url has passed, and keep it only when it is relative or its
// scheme is one the host allows.
std::optional<ustring> link_url(const ustring& url, const render_options& options);
std::optional<ustring> image_url(const ustring& url, const render_options& options);

std::string rel_attr(const render_options& options);

// Wraps `body` in a link, and records the link; gives `body` as it is where the link is not allowed.
std::string linked(const std::string& body, const ustring& url, const ustring& id, std::vector<rendered_link>& links, const render_options& options,
                   const std::string& attrs = "");

}  // namespace pele
