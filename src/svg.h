#pragma once

#include <string>
#include <string_view>

#include "icons.h"
#include "label.h"
#include "ustring.h"

namespace pele::svg {

// Escapes text for XML and drops the control characters XML does not allow.
std::string esc(std::string_view utf8);
std::string esc(ustring_view text);

// Escapes text that is only ever read as text: a tooltip, an accessible name, a `data-id`. Entity
// codes are turned into their characters first. A value that was checked before it is written,
// such as a style, a colour or a URL, must go through esc() instead, so that what was checked is
// what is written.
std::string esc_text(ustring_view text);
std::string esc_text(std::string_view utf8);

// A coordinate with at most two decimals.
std::string num(double value);

// Writes a measured label as SVG text centred on (cx, cy).
std::string label_svg(const label& l, double cx, double cy, std::string attrs, const icon_set* icons = nullptr);

std::string span_style(int style);

// The emphasis every span of a label has. It is written once, on the text element, where a CSS
// rule for the label reaches it.
int shared_style(const label& l);

// Adds that emphasis to a text element's attributes, unless they give a weight of their own.
std::string apply_shared_style(std::string attrs, int shared);

}  // namespace pele::svg
