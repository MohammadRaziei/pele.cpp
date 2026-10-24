#include "common/outline.h"

#include <algorithm>
#include <cstddef>

#include "chars.h"
#include "errors.h"
#include "regex.h"

namespace pele::outline {
namespace {

enum state : int { S_INITIAL, S_CLASS, S_ICON, S_NODE, S_NSTR, S_NSTR2, S_DATA, S_DATA_STR };

const js_regex& re_space() {
  static const js_regex re(R"(\s+)");
  return re;
}
const js_regex& re_line() {
  static const js_regex re(R"(.+)");
  return re;
}
const js_regex& re_id() {
  static const js_regex re(R"([^([\n){}]+)");
  return re;
}
const js_regex& re_id_kanban() {
  static const js_regex re(R"([^([\n){}@]+)");
  return re;
}
const js_regex& re_descr() {
  static const js_regex re(R"([^)\](}]+)");
  return re;
}
const js_regex& re_md() {
  static const js_regex re(R"([^`"]+)");
  return re;
}
const js_regex& re_data() {
  static const js_regex re(R"([^}^"]+)");
  return re;
}
const js_regex& re_br() {
  static const js_regex re(R"(\n\s*)");
  return re;
}

// `src.charCodeAt(i)`, with -1 in place of NaN past the end.
int code_at(ustring_view src, std::size_t i) { return i < src.size() ? src[i] : -1; }

// Jison's case-insensitive flag folds ASCII letters only.
bool letters(ustring_view src, std::size_t p, const char* word) {
  for (std::size_t k = 0; word[k] != '\0'; k++)
    if ((code_at(src, p + k) | 32) != word[k]) return false;
  return true;
}

}  // namespace

std::string token_name(int type, bool kanban) {
  static const char* const names[] = {"1", "INVALID", "SPACELINE", "NL", "EOF", "", "SPACELIST", "ICON", "CLASS", "NODE_DSTART", "NODE_ID", "NODE_DESCR", "NODE_DEND", "SHAPE_DATA"};
  if (type == T_KEYWORD) return kanban ? "KANBAN" : "MINDMAP";
  return type >= 0 && type <= T_SHAPE_DATA ? names[type] : "undefined";
}

int get_type(ustring_view start, ustring_view end) {
  if (start == u"[") return NT_RECT;
  if (start == u"(") return end == u")" ? NT_ROUNDED_RECT : NT_CLOUD;
  if (start == u"((") return NT_CIRCLE;
  if (start == u")") return NT_CLOUD;
  if (start == u"))") return NT_BANG;
  if (start == u"{{") return NT_HEXAGON;
  return NT_DEFAULT;
}

tokens tokenize(ustring_view src, bool kanban) {
  const std::size_t n = src.size();
  tokens out;
  const char* const keyword = kanban ? "kanban" : "mindmap";
  const std::size_t keyword_length = kanban ? 6 : 7;
  const js_regex& re_node_id = kanban ? re_id_kanban() : re_id();
  int st = S_INITIAL;
  std::size_t p = 0;
  std::ptrdiff_t next_break = -1;

  const auto put = [&](int type, ustring text, std::size_t end) {
    out.types.push_back(type);
    out.texts.push_back(std::move(text));
    out.starts.push_back(p);
    p = end;
  };
  const auto emit = [&](int type, std::size_t end, int next) {
    put(type, ustring(src.substr(p, end - p)), end);
    st = next;
  };
  // A sticky match at `from`: where it ends, or -1.
  const auto at = [&](const js_regex& re, std::size_t from) -> std::ptrdiff_t {
    if (from > n) return -1;
    const auto m = re.match_at(src, from);
    return m ? static_cast<std::ptrdiff_t>(m->end) : -1;
  };
  const auto up_to = [&](char16_t ch) -> std::size_t {
    const std::size_t e = src.find(ch, p);
    return e == ustring_view::npos ? n : e;
  };

  while (p < n) {
    const int c = code_at(src, p);
    const int d = code_at(src, p + 1);
    std::ptrdiff_t e;
    if (st == S_INITIAL) {
      const std::size_t w = c <= 32 || c >= 160 ? static_cast<std::size_t>(std::max(at(re_space(), p), static_cast<std::ptrdiff_t>(p))) : p;
      if (kanban && c == 64 && d == 123) {
        put(T_SHAPE_DATA, ustring(), p + 2);
        st = S_DATA;
      } else if (code_at(src, w) == 37 && code_at(src, w + 1) == 37) {
        emit(T_SPACELINE, static_cast<std::size_t>(at(re_line(), w)), S_INITIAL);
      } else if (w > p) {
        if (next_break < static_cast<std::ptrdiff_t>(p)) next_break = static_cast<std::ptrdiff_t>(up_to(u'\n'));
        if (next_break >= static_cast<std::ptrdiff_t>(w)) {
          emit(T_SPACELIST, w, S_INITIAL);
        } else {
          // `src.lastIndexOf('\n', w - 1)`: the last line break before the end of the blanks.
          const std::size_t last_break = src.rfind(u'\n', w - 1);
          if (last_break != ustring_view::npos && last_break > p) emit(T_SPACELINE, last_break + 1, S_INITIAL);
          else emit(T_NL, p + 1, S_INITIAL);
        }
      } else if (letters(src, p, keyword) && !is_word(code_at(src, p + keyword_length))) {
        emit(T_KEYWORD, p + keyword_length, S_INITIAL);
      } else if (c == 58 && d == 58 && code_at(src, p + 2) == 58) {
        p += 3;
        st = S_CLASS;
      } else if (c == 58 && d == 58 && letters(src, p + 2, "icon") && code_at(src, p + 6) == 40) {
        p += 7;
        st = S_ICON;
      } else if ((c == 45 && d == 41) || (c == 40 && (d == 45 || d == 40)) || ((c == 41 || c == 123) && d == c)) {
        emit(T_NODE_DSTART, p + 2, S_NODE);
      } else if (c == 41 || c == 40 || c == 91) {
        emit(T_NODE_DSTART, p + 1, S_NODE);
      } else if ((e = at(re_node_id, p)) != -1) {
        emit(T_NODE_ID, static_cast<std::size_t>(e), S_INITIAL);
      } else {
        break;
      }
    } else if (st == S_NODE) {
      if (c == 34) {
        st = d == 96 ? S_NSTR2 : S_NSTR;
        p += d == 96 ? 2 : 1;
      } else if (c == 41 || c == 40) {
        emit(T_NODE_DEND, d == c || (c == 40 && d == 45) ? p + 2 : p + 1, S_INITIAL);
      } else if (c == 93) {
        emit(T_NODE_DEND, p + 1, S_INITIAL);
      } else if ((c == 125 && d == 125) || (c == 45 && d == 41)) {
        emit(T_NODE_DEND, p + 2, S_INITIAL);
      } else {
        // A lone closing brace takes the rest of its line as text.
        e = at(c == 125 ? re_line() : re_descr(), p);
        if (e < 0) break;
        emit(T_NODE_DESCR, static_cast<std::size_t>(e), S_NODE);
      }
    } else if (st == S_NSTR) {
      if (c == 34) {
        p++;
        st = S_NODE;
      } else {
        emit(T_NODE_DESCR, up_to(u'"'), S_NSTR);
      }
    } else if (st == S_NSTR2) {
      if (c == 96 && d == 34) {
        p += 2;
        st = S_NODE;
      } else if ((e = at(re_md(), p)) != -1) {
        emit(T_NODE_DESCR, static_cast<std::size_t>(e), S_NSTR2);
      } else {
        break;
      }
    } else if (st == S_CLASS) {
      if ((e = at(re_line(), p)) != -1) {
        emit(T_CLASS, static_cast<std::size_t>(e), S_INITIAL);
      } else if (c == 10) {
        p++;
        st = S_INITIAL;
      } else {
        break;
      }
    } else if (st == S_ICON) {
      if (c == 41) {
        p++;
        st = S_INITIAL;
      } else {
        emit(T_ICON, up_to(u')'), S_ICON);
      }
    } else if (st == S_DATA) {
      if (c == 34) {
        emit(T_SHAPE_DATA, p + 1, S_DATA_STR);
      } else if (c == 125) {
        p++;
        st = S_INITIAL;
      } else if ((e = at(re_data(), p)) != -1) {
        emit(T_SHAPE_DATA, static_cast<std::size_t>(e), S_DATA);
      } else {
        break;
      }
    } else if (c == 34) {
      emit(T_SHAPE_DATA, p + 1, S_DATA);
    } else {
      const std::size_t end = up_to(u'"');
      put(T_SHAPE_DATA, re_br().replace_all(src.substr(p, end - p), ustring_view(u"<br/>")), end);
    }
  }

  if (p < n) {
    put(T_ERROR, ustring(), p);
  } else {
    if (st == S_INITIAL) put(T_EOF, ustring(), n);
    put(T_END, ustring(), n);
  }
  return out;
}

namespace {

bool is_stop(int type) { return type == T_SPACELINE || type == T_NL || type == T_EOF; }

bool starts_item(int type) {
  return type == T_SPACELINE || type == T_SPACELIST || type == T_ICON || type == T_CLASS || type == T_NODE_DSTART || type == T_NODE_ID;
}

}  // namespace

void parse_outline(const ustring& src, outline_db& db, bool kanban) {
  const std::string diagram = kanban ? "kanban" : "mindmap";
  const tokens tk = tokenize(src, kanban);
  const std::string data = kanban ? ", 'SHAPE_DATA'" : "";
  std::size_t i = 0;
  const auto type = [&](std::size_t k) { return k < tk.types.size() ? tk.types[k] : -1; };
  const auto text = [&](std::size_t k) -> const ustring& {
    static const ustring none;
    return k < tk.texts.size() ? tk.texts[k] : none;
  };

  const auto fail = [&](const std::string& expected) {
    const std::size_t at = std::min(i, tk.starts.size() - 1);
    const int t = type(i);
    if (t == T_ERROR) throw syntax_error(diagram, src, tk.starts[at], u"", true);
    throw syntax_error(diagram, src, tk.starts[at], to_utf16("Expecting " + expected + ", got '" + token_name(t, kanban) + "'"));
  };

  const std::string stop = "'SPACELINE', 'NL', 'EOF'";
  const std::string keyword = "'" + token_name(T_KEYWORD, kanban) + "'";
  if (type(i) == T_SPACELINE) {
    i++;
    while (type(i) == T_SPACELINE || type(i) == T_NL) i++;
    if (type(i) != T_KEYWORD) fail("'SPACELINE', 'NL', " + keyword);
  } else if (type(i) != T_KEYWORD) {
    fail("'SPACELINE', " + keyword);
  }
  i++;
  std::string first = "'SPACELINE', 'NL', 'SPACELIST', 'ICON', 'CLASS', 'NODE_DSTART', 'NODE_ID'";
  if (type(i) == T_NL) {
    i++;
    first = "'SPACELINE', 'SPACELIST', 'ICON', 'CLASS', 'NODE_DSTART', 'NODE_ID'";
  }

  for (;;) {
    int t = type(i);
    int level = 0;
    const bool indented = t == T_SPACELIST;
    if (indented) {
      level = static_cast<int>(text(i++).size());
      t = type(i);
    }
    if (t == T_NODE_ID || t == T_NODE_DSTART) {
      std::optional<ustring> id;
      ustring descr;
      int node = 0;
      if (t == T_NODE_ID) id = text(i++);
      if (type(i) == T_NODE_DSTART) {
        const ustring start = text(i++);
        if (type(i) != T_NODE_DESCR) fail("'NODE_DESCR'");
        descr = text(i++);
        if (type(i) != T_NODE_DEND) fail("'NODE_DEND'");
        const ustring end = text(i++);
        if (!is_stop(type(i)) && type(i) != T_SHAPE_DATA) fail(stop + data);
        node = db.type_of(start, end);
      } else {
        if (!is_stop(type(i)) && type(i) != T_SHAPE_DATA) fail(stop + ", 'NODE_DSTART'" + data);
        descr = *id;
      }
      if (type(i) == T_SHAPE_DATA) {
        ustring shape_data = text(i++);
        while (type(i) == T_SHAPE_DATA) shape_data += text(i++);
        if (!is_stop(type(i))) fail(stop + data);
        db.add_node(level, id ? *id : descr, descr, node, shape_data);
      } else {
        db.add_node(level, id ? *id : descr, descr, node, std::nullopt);
      }
    } else if (t == T_ICON || t == T_CLASS) {
      const ustring value = text(i++);
      if (!is_stop(type(i))) fail(stop);
      if (t == T_ICON) db.decorate_node(value, std::nullopt);
      else db.decorate_node(std::nullopt, value);
    } else if (indented) {
      if (!is_stop(t)) fail(stop + ", 'ICON', 'CLASS', 'NODE_DSTART', 'NODE_ID'");
    } else if (t == T_SPACELINE) {
      if (!is_stop(type(++i))) fail(stop);
    } else {
      fail(first);
    }

    i++;
    while (type(i) == T_NL || type(i) == T_EOF) i++;
    if (type(i) == T_END) return;
    if (!starts_item(type(i))) fail(stop + ", 'SPACELIST', 'ICON', 'CLASS', 'NODE_DSTART', 'NODE_ID'");
  }
}

}  // namespace pele::outline
