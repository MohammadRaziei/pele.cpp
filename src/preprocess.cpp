#include "preprocess.h"

#include <algorithm>

#include "chars.h"
#include "errors.h"

namespace pele {
namespace {

bool is_line_break(int c) { return c == '\n' || c == '\r'; }

int at(const ustring& s, std::size_t i) { return i < s.size() ? s[i] : -1; }

bool starts_with_at(const ustring& s, const ustring& prefix, std::size_t i) { return i <= s.size() && s.compare(i, prefix.size(), prefix) == 0; }

// String.prototype.lastIndexOf(needle, from) with -1 for none.
long long last_index_of(const ustring& s, const ustring& needle, long long from) {
  if (from < 0) from = 0;
  const std::size_t found = s.rfind(needle, static_cast<std::size_t>(from));
  return found == ustring::npos ? -1 : static_cast<long long>(found);
}

long long index_of(const ustring& s, const ustring& needle, std::size_t from) {
  const std::size_t found = s.find(needle, from);
  return found == ustring::npos ? -1 : static_cast<long long>(found);
}

struct front_matter {
  ustring indent;
  ustring body;
  std::size_t length;
};

// Finds a `---` fenced YAML block at the start of the text the way Mermaid's front matter regex does.
std::optional<front_matter> match_front_matter(const ustring& text) {
  std::size_t cursor = 0;
  while (cursor < text.size() && is_space(text[cursor]) && !is_line_break(text[cursor])) cursor++;
  const ustring indent = text.substr(0, cursor);
  if (!starts_with_at(text, u"---", cursor)) return std::nullopt;

  std::vector<std::size_t> opening_ends;
  for (std::size_t i = cursor + 3; i < text.size() && is_space(text[i]); i++)
    if (is_line_break(text[i])) opening_ends.push_back(i);
  if (opening_ends.empty()) return std::nullopt;

  const ustring fence = indent + u"---";
  auto closing_end = [&](std::size_t i) -> long long {
    if (!is_line_break(at(text, i)) || !starts_with_at(text, fence, i + 1)) return -1;
    long long last = -1;
    for (std::size_t j = i + 1 + fence.size(); j < text.size() && is_space(text[j]); j++)
      if (is_line_break(text[j])) last = static_cast<long long>(j);
    return last;
  };

  const ustring needle = u"\n" + fence;
  long long last_fence = -1;
  for (long long i = last_index_of(text, needle, static_cast<long long>(text.size())); i > static_cast<long long>(opening_ends[0]);
       i = last_index_of(text, needle, i - 1)) {
    if (closing_end(static_cast<std::size_t>(i)) != -1) {
      last_fence = i;
      break;
    }
  }
  if (last_fence == -1) return std::nullopt;

  std::size_t opening_end = opening_ends[0];
  for (std::size_t candidate : opening_ends)
    if (static_cast<long long>(candidate) < last_fence) opening_end = candidate;

  const std::size_t body_start = opening_end + 1;
  const std::size_t first_at = body_start > 0 ? body_start - 1 : 0;
  for (long long i = index_of(text, needle, first_at); i != -1 && i <= last_fence; i = index_of(text, needle, static_cast<std::size_t>(i) + 1)) {
    if (static_cast<std::size_t>(i) < body_start) continue;
    const long long end = closing_end(static_cast<std::size_t>(i));
    if (end != -1) return front_matter{indent, text.substr(body_start, static_cast<std::size_t>(i) - body_start), static_cast<std::size_t>(end) + 1};
  }
  return std::nullopt;
}

bool unsafe_key(ustring_view key) { return key == u"__proto__" || key == u"constructor" || key == u"prototype"; }

void merge(yaml_value& target, const yaml_value& source) {
  for (const ustring& key : source.keys()) {
    if (unsafe_key(key)) continue;
    const yaml_value& value = *source.find(key);
    yaml_value* current = target.find(key);
    if (value.is_object() && current != nullptr && current->is_object()) {
      merge(*current, value);
    } else if (value.is_object()) {
      yaml_value fresh = yaml_value::object();
      merge(fresh, value);
      target.set(key, std::move(fresh));
    } else {
      target.set(key, value);
    }
  }
}

// One `%%{ ... }%%` directive, as Mermaid's pattern reads it.
struct directive {
  std::size_t end = 0;
  ustring type;
  // The text after the type that is not a single word; empty when there is none.
  ustring body;
};

std::size_t skip_space(const ustring& t, std::size_t i) {
  while (i < t.size() && is_space(t[i])) i++;
  return i;
}

std::size_t skip_word(const ustring& t, std::size_t i) {
  while (i < t.size() && is_word(t[i])) i++;
  return i;
}

// `%{2}{\s*(?:(\w+)\s*:|(\w+))\s*(?:(\w+)|((?:(?!}%{2}).|\r?\n)*))?\s*(?:}%{2})?` at i, or false.
bool match_directive(const ustring& t, std::size_t i, directive& out) {
  if (!starts_with_at(t, u"%%{", i)) return false;
  const std::size_t j = skip_space(t, i + 3);
  const std::size_t word_end = skip_word(t, j);
  if (word_end == j) return false;
  std::size_t pos;
  const std::size_t after_space = skip_space(t, word_end);
  if (at(t, after_space) == ':') {
    out.type = t.substr(j, word_end - j);
    pos = after_space + 1;
  } else {
    out.type = t.substr(j, word_end - j);
    pos = word_end;
  }
  pos = skip_space(t, pos);
  out.body.clear();
  if (pos < t.size() && is_word(t[pos])) {
    pos = skip_word(t, pos);
  } else {
    const std::size_t body_start = pos;
    while (pos < t.size()) {
      if (starts_with_at(t, u"}%%", pos)) break;
      const char16_t c = t[pos];
      if (!is_line_end(c)) {
        pos++;
      } else if (c == '\n') {
        pos++;
      } else if (c == '\r' && at(t, pos + 1) == '\n') {
        pos += 2;
      } else {
        break;
      }
    }
    out.body = t.substr(body_start, pos - body_start);
  }
  pos = skip_space(t, pos);
  if (starts_with_at(t, u"}%%", pos)) pos += 3;
  out.end = pos;
  return true;
}

ustring directives(const ustring& text, yaml_value& settings) {
  if (text.find(u"%%{") == ustring::npos) return text;
  ustring normalized = trim(text);
  for (char16_t& c : normalized)
    if (c == '\'') c = '"';
  for (std::size_t i = normalized.find(u"%%{"); i != ustring::npos; i = normalized.find(u"%%{", i + 1)) {
    directive d;
    if (!match_directive(normalized, i, d)) continue;
    if (d.type == u"wrap") {
      settings.set(u"wrap", yaml_value::boolean(true));
    } else if ((d.type == u"init" || d.type == u"initialize") && !d.body.empty()) {
      try {
        const yaml_value args = parse_json(trim(d.body));
        if (args.is_object()) merge(settings, args);
      } catch (const yaml_error&) {
        // Mermaid ignores an init directive whose body is not valid JSON.
      }
    }
    i = d.end > i ? d.end - 1 : i;
  }
  ustring out;
  std::size_t last = 0;
  for (std::size_t i = text.find(u"%%{"); i != ustring::npos; i = text.find(u"%%{", i + 1)) {
    directive d;
    if (!match_directive(text, i, d)) continue;
    out += text.substr(last, i - last);
    last = d.end;
    i = d.end > i ? d.end - 1 : i;
  }
  return out + text.substr(last);
}

// Quotes the values of the attributes of HTML tags with apostrophes, as Mermaid does.
ustring requote_attributes(const ustring& attrs) {
  ustring out;
  for (std::size_t i = 0; i < attrs.size();) {
    if (attrs[i] == '=' && at(attrs, i + 1) == '"') {
      const std::size_t close = attrs.find(u'"', i + 2);
      if (close != ustring::npos) {
        out += u"='" + attrs.substr(i + 2, close - i - 2) + u"'";
        i = close + 1;
        continue;
      }
    }
    out += attrs[i++];
  }
  return out;
}

ustring rewrite_tags(const ustring& text) {
  const std::size_t last_close_found = text.find(u'<') != ustring::npos ? text.rfind(u'>') : ustring::npos;
  if (last_close_found == ustring::npos || last_close_found == 0) return text;
  const ustring head = text.substr(0, last_close_found + 1);
  ustring out;
  std::size_t i = 0;
  while (i < head.size()) {
    if (head[i] == '<' && i + 1 < head.size() && is_word(head[i + 1])) {
      const std::size_t name_end = skip_word(head, i + 1);
      const std::size_t close = head.find(u'>', name_end);
      if (close != ustring::npos) {
        out += u"<" + head.substr(i + 1, name_end - i - 1) + requote_attributes(head.substr(name_end, close - name_end)) + u">";
        i = close + 1;
        continue;
      }
    }
    out += head[i++];
  }
  return out + text.substr(last_close_found + 1);
}

// Mermaid's `String(text)` for a front matter value.
ustring to_js_string(const yaml_value& v) { return v.to_js_string(); }

}  // namespace

ustring cleanup_comments(const ustring& text) {
  if (text.find(u"%%") == ustring::npos) return trim_start(text);
  // /^\s*%%(?!{)[^\n]+\n?/gm
  auto line_start = [&](std::size_t i) { return i == 0 || is_line_end(text[i - 1]); };
  ustring out;
  std::size_t last = 0;
  std::size_t i = 0;
  while (i < text.size()) {
    if (!line_start(i)) {
      i++;
      continue;
    }
    const std::size_t q = skip_space(text, i);
    bool matched = false;
    if (starts_with_at(text, u"%%", q) && at(text, q + 2) != '{') {
      std::size_t k = q + 2;
      while (k < text.size() && text[k] != '\n') k++;
      if (k > q + 2) {
        if (k < text.size()) k++;
        out += text.substr(last, i - last);
        last = k;
        i = k;
        matched = true;
      }
    }
    // Other line starts inside the same run of whitespace would reach the same place.
    if (!matched) i = q > i ? q : i + 1;
  }
  return trim_start(out + text.substr(last));
}

preprocessed preprocess(ustring_view source) {
  ustring text;
  if (source.find(u'\r') != ustring_view::npos) {
    for (std::size_t i = 0; i < source.size(); i++) {
      if (source[i] == '\r') {
        text += u'\n';
        if (i + 1 < source.size() && source[i + 1] == '\n') i++;
      } else {
        text += source[i];
      }
    }
  } else {
    text = ustring(source);
  }
  // A tag needs a closing bracket, so nothing after the last one can match.
  text = rewrite_tags(text);

  preprocessed result;
  int line_offset = 0;
  std::optional<front_matter> front;
  if (text.find(u"---") != ustring::npos) front = match_front_matter(text);
  if (front) {
    ustring body;
    if (!front->indent.empty()) {
      std::size_t from = 0;
      bool first = true;
      for (;;) {
        const std::size_t nl = front->body.find(u'\n', from);
        const ustring line = front->body.substr(from, nl == ustring::npos ? ustring::npos : nl - from);
        if (!first) body += u'\n';
        first = false;
        body += starts_with_at(line, front->indent, 0) ? line.substr(front->indent.size()) : line;
        if (nl == ustring::npos) break;
        from = nl + 1;
      }
    } else {
      body = front->body;
    }
    yaml_value parsed;
    try {
      parsed = parse_yaml(body);
    } catch (const yaml_error& error) {
      throw pele_error(std::string("Front matter is not valid YAML. ") + error.what(), error_code::syntax);
    }
    if (parsed.is_object()) {
      const yaml_value* title = parsed.find("title");
      if (title != nullptr && title->truthy()) result.title = to_js_string(*title);
      const yaml_value* cfg = parsed.find("config");
      if (cfg != nullptr && cfg->is_object()) merge(result.settings, *cfg);
      const yaml_value* mode = parsed.find("displayMode");
      if (mode != nullptr && mode->truthy()) {
        yaml_value* gantt = result.settings.find("gantt");
        if (gantt == nullptr || !gantt->is_object()) {
          result.settings.set(u"gantt", yaml_value::object());
          gantt = result.settings.find("gantt");
        }
        gantt->set(u"displayMode", yaml_value::string(to_js_string(*mode)));
      }
    }
    for (std::size_t i = text.find(u'\n'); i != ustring::npos && i < front->length; i = text.find(u'\n', i + 1)) line_offset++;
    text = text.substr(front->length);
  }

  text = directives(text, result.settings);
  result.text = cleanup_comments(text);
  result.with_comments = text;
  result.line_offset = line_offset;
  return result;
}

namespace {

bool is_hash_entity_int(ustring_view inner) {
  // /^\+?\d+$/
  std::size_t i = 0;
  if (i < inner.size() && inner[i] == '+') i++;
  if (i >= inner.size()) return false;
  for (; i < inner.size(); i++)
    if (!is_digit(inner[i])) return false;
  return true;
}

// Does what Mermaid's `text.replace(/keyword.*:\S*#.*;/g, drop the last character)` does: on a
// line where the keyword is followed by a colon, a colour-like `#`, and later a semicolon, the
// last semicolon goes. Mermaid's regex backtracks without bound; this is one pass.
ustring drop_color_semicolons(const ustring& text, const ustring& keyword) {
  std::size_t from = text.find(keyword);
  if (from == ustring::npos) return text;
  ustring out;
  std::size_t last = 0;
  bool any = false;
  while (from != ustring::npos) {
    std::size_t line_end = from;
    while (line_end < text.size() && !is_line_end(text[line_end])) line_end++;
    bool after_colon = false;
    long long hash = -1;
    for (std::size_t j = from + keyword.size(); j < line_end; j++) {
      const int c = text[j];
      if (c == 58) {
        after_colon = true;
      } else if (c == 35) {
        if (after_colon) {
          hash = static_cast<long long>(j);
          break;
        }
      } else if (is_space(c)) {
        after_colon = false;
      }
    }
    if (hash != -1 && line_end > 0) {
      const std::size_t semi = text.rfind(u';', line_end - 1);
      if (semi != ustring::npos && static_cast<long long>(semi) > hash) {
        out += text.substr(last, semi - last);
        last = semi + 1;
        any = true;
      }
    }
    from = line_end >= text.size() ? ustring::npos : text.find(keyword, line_end + 1);
  }
  return any ? out + text.substr(last) : text;
}

}  // namespace

ustring encode_entities(const ustring& text) {
  if (text.find(u'#') == ustring::npos) return text;
  const ustring dropped = drop_color_semicolons(drop_color_semicolons(text, u"style"), u"classDef");
  ustring out;
  for (std::size_t i = 0; i < dropped.size();) {
    if (dropped[i] == '#') {
      const std::size_t end = skip_word(dropped, i + 1);
      if (end > i + 1 && at(dropped, end) == ';') {
        const ustring inner = dropped.substr(i + 1, end - i - 1);
        out += is_hash_entity_int(inner) ? ustring{0xFB02, 0x00B0, 0x00B0} : ustring{0xFB02, 0x00B0};
        out += inner;
        out += ustring{0x00B6, 0x00DF};
        i = end + 1;
        continue;
      }
    }
    out += dropped[i++];
  }
  return out;
}

}  // namespace pele
