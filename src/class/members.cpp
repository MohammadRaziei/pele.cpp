#include "class/members.h"

#include "chars.h"
#include "common/generics.h"

namespace pele::classdiagram {
namespace {

bool is_visibility(char16_t c) { return c == u'#' || c == u'+' || c == u'~' || c == u'-'; }

// `text.slice(from, to)` for indices that are not negative.
ustring slice(ustring_view text, std::ptrdiff_t from, std::ptrdiff_t to) {
  const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(text.size());
  from = from < 0 ? 0 : from > n ? n : from;
  to = to < 0 ? 0 : to > n ? n : to;
  return from >= to ? ustring() : ustring(text.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from)));
}

// `text.substring(a, b)`: the arguments are clamped and put in order.
ustring substring(ustring_view text, std::ptrdiff_t a, std::ptrdiff_t b) {
  const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(text.size());
  a = a < 0 ? 0 : a > n ? n : a;
  b = b < 0 ? 0 : b > n ? n : b;
  if (a > b) std::swap(a, b);
  return ustring(text.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a)));
}

}  // namespace

class_member::class_member(ustring_view input, kind type) : member_type(type) {
  // Mermaid throws a TypeError for a method with no opening parenthesis, such as `run)`.
  // It is kept here as an attribute, so it is shown as written.
  if (type != kind::method || !parse_method(input)) parse_attribute(input);
  id = !id.empty() && id[0] == u' ' ? ustring(u" ") + trim(id) : trim(id);
}

// The same split as Mermaid's /([#+~-])?(.+)\((.*)\)([\s$*])?(.*)([$*])?/, found in one pass:
// the first line with an opening parenthesis after its first character and a closing one later.
bool class_member::parse_method(ustring_view input) {
  const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(input.size());
  for (std::ptrdiff_t start = 0; start < n;) {
    std::ptrdiff_t end = start;
    while (end < n && !is_line_end(input[static_cast<std::size_t>(end)])) end++;
    std::ptrdiff_t close = end - 1;
    while (close > start && input[static_cast<std::size_t>(close)] != 41) close--;
    std::ptrdiff_t open = close - 1;
    while (open > start && input[static_cast<std::size_t>(open)] != 40) open--;
    if (open > start) {
      const char16_t first = input[static_cast<std::size_t>(start)];
      const bool visible = is_visibility(first) && open > start + 1;
      if (visible) visibility = ustring(1, first);
      id = slice(input, visible ? start + 1 : start, open);
      parameters = trim(slice(input, open + 1, close));
      std::ptrdiff_t rest = close + 1;
      ustring cls;
      if (rest < n) {
        const char16_t next = input[static_cast<std::size_t>(rest)];
        if (next == u'$' || next == u'*' || trim(ustring(1, next)).empty()) {
          cls = trim(ustring(1, next));
          rest++;
        }
      }
      std::ptrdiff_t stop = rest;
      while (stop < n && !is_line_end(input[static_cast<std::size_t>(stop)])) stop++;
      return_type = trim(slice(input, rest, stop));
      if (cls.empty() && !return_type.empty()) {
        const char16_t last = return_type.back();
        if (last == u'$' || last == u'*') {
          cls = ustring(1, last);
          return_type.pop_back();
        }
      }
      classifier = cls;
      return true;
    }
    start = end + 1;
  }
  member_type = kind::attribute;
  return false;
}

void class_member::parse_attribute(ustring_view input) {
  if (!input.empty() && is_visibility(input[0])) visibility = ustring(1, input[0]);
  if (!input.empty() && (input.back() == u'$' || input.back() == u'*')) classifier = ustring(1, input.back());
  id = substring(input, visibility.empty() ? 0 : 1, classifier.empty() ? static_cast<std::ptrdiff_t>(input.size()) : static_cast<std::ptrdiff_t>(input.size()) - 1);
}

class_member::display class_member::display_details() const {
  ustring text = visibility + parse_generic_types(id);
  if (member_type == kind::method) {
    text += u"(" + parse_generic_types(trim(parameters)) + u")";
    if (!return_type.empty()) text += u" : " + parse_generic_types(return_type);
  }
  return {trim(text), parse_classifier()};
}

std::string class_member::parse_classifier() const {
  return classifier == u"*" ? "font-style:italic;" : classifier == u"$" ? "text-decoration:underline;" : "";
}

}  // namespace pele::classdiagram
