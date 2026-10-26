#include "class/lexer.h"

#include <cstddef>

#include "chars.h"
#include "class/tokens.h"
#include "regex.h"
#include "unicode_letter.h"

namespace pele::classdiagram {
namespace {

enum state : int { S_INITIAL, S_string, S_bqstring, S_generic, S_callbackName, S_callbackArgs, S_accTitle, S_accDescr, S_accDescrMultiline, S_cls, S_classBody, S_namespace, S_namespaceBody };

// The token each state gives at the end of input, before the parser sees the end itself.
const int kAtEnd[] = {T_EOF, T_STR, T_EOF, T_GENERICTYPE, T_CALLBACK_NAME, T_CALLBACK_ARGS, T_acc_title_value, T_acc_descr_value, T_acc_descr_multiline_value, T_EOF, T_EOF_IN_STRUCT, T_EOF, T_EOF_IN_STRUCT};

const js_regex& re_direction(int i) {
  static const js_regex res[] = {js_regex(R"(.*direction\s+TB[^\n]*)"), js_regex(R"(.*direction\s+BT[^\n]*)"), js_regex(R"(.*direction\s+RL[^\n]*)"), js_regex(R"(.*direction\s+LR[^\n]*)")};
  return res[i];
}

}  // namespace

tokens tokenize(ustring_view src) {
  const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(src.size());
  tokens out;
  std::vector<int> stack;
  int st = S_INITIAL;
  std::ptrdiff_t p = 0;
  const std::size_t first_direction = src.find(u"direction");
  std::ptrdiff_t dir_next = first_direction == ustring_view::npos ? -1 : static_cast<std::ptrdiff_t>(first_direction);
  std::ptrdiff_t line_end = -1;
  std::ptrdiff_t dir_failed = 0;

  // `src.charCodeAt(i)`, with -1 in place of NaN past the end.
  const auto code_at = [&](std::ptrdiff_t i) -> int { return i >= 0 && i < n ? src[static_cast<std::size_t>(i)] : -1; };
  const auto emit = [&](int type, std::ptrdiff_t s, std::ptrdiff_t e) -> bool {
    out.types.push_back(type);
    out.texts.push_back(ustring(src.substr(static_cast<std::size_t>(s), static_cast<std::size_t>(e - s))));
    out.starts.push_back(static_cast<std::size_t>(s));
    p = e;
    return true;
  };
  const auto push = [&](int s) {
    stack.push_back(st);
    st = s;
  };
  const auto pop = [&]() {
    if (!stack.empty()) {
      st = stack.back();
      stack.pop_back();
    }
  };
  const auto starts_with = [&](const char16_t* word, std::ptrdiff_t at) {
    std::size_t k = 0;
    for (; word[k] != u'\0'; k++)
      if (code_at(at + static_cast<std::ptrdiff_t>(k)) != word[k]) return false;
    return true;
  };
  const auto length_of = [](const char16_t* word) {
    std::ptrdiff_t k = 0;
    while (word[k] != u'\0') k++;
    return k;
  };
  const auto kw = [&](const char16_t* word) { return starts_with(word, p) && !is_word(code_at(p + length_of(word))); };
  const auto skip_ws = [&](std::ptrdiff_t q) {
    while (q < n && is_space(code_at(q))) q++;
    return q;
  };
  const auto until = [&](char16_t ch) -> std::ptrdiff_t {
    const std::size_t e = src.find(ch, static_cast<std::size_t>(p));
    return e == ustring_view::npos ? n : static_cast<std::ptrdiff_t>(e);
  };

  // A run of whitespace is a NEWLINE up to its last line feed; the rest is skipped.
  const auto space = [&](bool leaves) {
    std::ptrdiff_t q = p;
    std::ptrdiff_t k = -1;
    for (int c = code_at(q); q < n && is_space(c); c = code_at(++q))
      if (c == 10) k = q;
    if (k < 0) {
      p = q;
      return;
    }
    if (leaves) pop();
    emit(T_NEWLINE, p, k + 1);
  };

  // The keywords that every state recognizes, even inside generics and backtick names.
  const auto keyword = [&](int c) -> bool {
    switch (c) {
      case 99:
        if (kw(u"cssClass")) return emit(T_CSSCLASS, p, p + 8);
        if (kw(u"callback")) return emit(T_CALLBACK, p, p + 8);
        if (kw(u"click")) return emit(T_CLICK, p, p + 5);
        break;
      case 108:
        if (kw(u"link")) return emit(T_LINK, p, p + 4);
        break;
      case 110:
        if (kw(u"note for")) return emit(T_NOTE_FOR, p, p + 8);
        if (kw(u"note")) return emit(T_NOTE, p, p + 4);
        break;
      case 60:
        if (code_at(p + 1) == 60) return emit(T_ANNOTATION_START, p, p + 2);
        break;
      case 62:
        if (code_at(p + 1) == 62) return emit(T_ANNOTATION_END, p, p + 2);
        break;
      case 104:
        if (kw(u"href")) return emit(T_HREF, p, p + 4);
        break;
    }
    return false;
  };

  const auto common = [&](int c) -> bool {
    if (keyword(c)) return true;
    const int d = code_at(p + 1);
    switch (c) {
      case 34:
        push(S_string);
        p++;
        return true;
      case 126:
        push(S_generic);
        p++;
        return true;
      case 96:
        push(S_bqstring);
        p++;
        return true;
      case 95:
        if (kw(u"_self")) return emit(T_LINK_TARGET, p, p + 5);
        if (kw(u"_blank")) return emit(T_LINK_TARGET, p, p + 6);
        if (kw(u"_parent")) return emit(T_LINK_TARGET, p, p + 7);
        if (kw(u"_top")) return emit(T_LINK_TARGET, p, p + 4);
        break;
      case 60:
        return d == 124 ? emit(T_EXTENSION, p, p + 2) : emit(T_DEPENDENCY, p, p + 1);
      case 124:
        return d == 62 && emit(T_EXTENSION, p, p + 2);
      case 62:
        return emit(T_DEPENDENCY, p, p + 1);
      case 42:
        return emit(T_COMPOSITION, p, p + 1);
      case 111:
        if (!is_word(d)) return emit(T_AGGREGATION, p, p + 1);
        break;
      case 40:
        return d == 41 && emit(T_LOLLIPOP, p, p + 2);
      case 45:
        return d == 45 ? emit(T_LINE, p, p + 2) : emit(T_MINUS, p, p + 1);
      case 46:
        return d == 46 ? emit(T_DOTTED_LINE, p, p + 2) : emit(T_DOT, p, p + 1);
      case 58: {
        std::ptrdiff_t q = p + 1;
        while (q < n) {
          const int ch = code_at(q);
          if (ch == 58 || ch == 10 || ch == 59) break;
          q++;
        }
        if (q > p + 1) return emit(T_LABEL, p, q);
        return d == 58 && code_at(p + 2) == 58 && emit(T_STYLE_SEPARATOR, p, p + 3);
      }
      case 43:
        return emit(T_PLUS, p, p + 1);
      case 37:
        return emit(T_PCT, p, p + 1);
      case 61:
        return emit(T_EQUALS, p, p + 1);
      case 91:
        return emit(T_SQS, p, p + 1);
      case 93:
        return emit(T_SQE, p, p + 1);
      case 33:
      case 35:
      case 36:
      case 38:
      case 39:
      case 44:
      case 63:
      case 92:
      case 47:
        return emit(T_PUNCTUATION, p, p + 1);
    }
    if (is_word(c)) {
      std::ptrdiff_t q = p + 1;
      while (is_word(code_at(q))) q++;
      return emit(T_ALPHA, p, q);
    }
    return is_unicode_letter(c) && emit(T_UNICODE_TEXT, p, p + 1);
  };

  const auto direction = [&]() -> bool {
    if (dir_next == -1 || p < dir_failed) return false;
    if (dir_next < p) {
      const std::size_t f = src.find(u"direction", static_cast<std::size_t>(p));
      dir_next = f == ustring_view::npos ? -1 : static_cast<std::ptrdiff_t>(f);
    }
    if (dir_next == -1) return false;
    if (line_end < p) {
      // The next line terminator at or after p, or the end.
      std::ptrdiff_t e = p;
      while (e < n && !is_line_end(code_at(e))) e++;
      line_end = e;
    }
    if (dir_next >= line_end) return false;
    for (int i = 0; i < 4; i++) {
      if (const auto m = re_direction(i).match_at(src, static_cast<std::size_t>(p))) return emit(T_direction_tb + i, p, static_cast<std::ptrdiff_t>(m->end));
    }
    // No direction statement starts anywhere in the rest of this line either.
    dir_failed = line_end;
    return false;
  };

  const auto initial = [&](int c) -> bool {
    if (direction()) return true;
    if (is_space(c)) {
      space(false);
      return true;
    }
    switch (c) {
      case 37:
        if (code_at(p + 1) == 37) {
          std::ptrdiff_t e = until(u'\n');
          while (e < n && (code_at(e) == 10 || code_at(e) == 13)) e++;
          p = e;
          return true;
        }
        break;
      case 97: {
        const bool title = starts_with(u"accTitle", p);
        if (!title && !starts_with(u"accDescr", p)) break;
        const std::ptrdiff_t q = skip_ws(p + 8);
        const int d = code_at(q);
        if (d == 58) {
          emit(title ? T_acc_title : T_acc_descr, p, skip_ws(q + 1));
          push(title ? S_accTitle : S_accDescr);
          return true;
        }
        if (d == 123 && !title) {
          p = skip_ws(q + 1);
          push(S_accDescrMultiline);
          return true;
        }
        break;
      }
      case 99:
        if (kw(u"classDiagram-v2")) return emit(T_CLASS_DIAGRAM, p, p + 15);
        if (kw(u"classDiagram")) return emit(T_CLASS_DIAGRAM, p, p + 12);
        if (starts_with(u"call", p) && is_space(code_at(p + 4))) {
          p = skip_ws(p + 4);
          push(S_callbackName);
          return true;
        }
        if (kw(u"classDef")) return emit(T_CLASSDEF, p, p + 8);
        if (kw(u"class")) {
          push(S_cls);
          return emit(T_CLASS, p, p + 5);
        }
        break;
      case 91:
        if (starts_with(u"*]", p + 1)) return emit(T_EDGE_STATE, p, p + 3);
        break;
      case 115:
        if (kw(u"style")) return emit(T_STYLE, p, p + 5);
        break;
      case 110:
        if (kw(u"namespace")) {
          push(S_namespace);
          return emit(T_NAMESPACE, p, p + 9);
        }
        break;
      case 44:
        return emit(T_COMMA, p, p + 1);
      case 35:
        return emit(T_BRKT, p, p + 1);
    }
    return common(c) || (c == 58 && emit(T_COLON, p, p + 1));
  };

  for (;;) {
    if (p >= n) {
      out.types.push_back(kAtEnd[st]);
      out.texts.push_back(ustring());
      out.starts.push_back(static_cast<std::size_t>(n));
      break;
    }
    const int c = code_at(p);
    bool ok = true;
    switch (st) {
      case S_INITIAL:
        ok = initial(c);
        break;

      case S_cls:
        if (is_space(c)) {
          space(true);
        } else if (c == 125) {
          pop();
          pop();
          emit(T_STRUCT_STOP, p, p + 1);
        } else if (c == 123) {
          push(S_classBody);
          emit(T_STRUCT_START, p, p + 1);
        } else {
          ok = common(c);
        }
        break;

      case S_classBody:
        if (c == 34) {
          push(S_string);
          p++;
        } else if (c == 125) {
          pop();
          emit(T_STRUCT_STOP, p, p + 1);
        } else if (c == 91 && starts_with(u"*]", p + 1)) {
          emit(T_EDGE_STATE, p, p + 3);
        } else if (c == 123) {
          emit(T_OPEN_IN_STRUCT, p, p + 1);
        } else if (c == 10) {
          p++;
        } else {
          std::ptrdiff_t q = p + 1;
          while (q < n) {
            const int ch = code_at(q);
            if (ch == 123 || ch == 125 || ch == 10) break;
            q++;
          }
          emit(T_MEMBER, p, q);
        }
        break;

      case S_namespace:
        if (is_space(c)) {
          space(true);
        } else if (c == 123) {
          push(S_namespaceBody);
          emit(T_STRUCT_START, p, p + 1);
        } else if (c == 125) {
          // The brace is read again in the state the namespace was opened from.
          pop();
        } else if (c == 110 && kw(u"namespace")) {
          push(S_namespace);
          emit(T_NAMESPACE, p, p + 9);
        } else {
          ok = common(c);
        }
        break;

      case S_namespaceBody:
        if (is_space(c)) {
          space(false);
        } else if (c == 125) {
          pop();
          emit(T_STRUCT_STOP, p, p + 1);
        } else if (c == 110 && kw(u"namespace")) {
          push(S_namespace);
          emit(T_NAMESPACE, p, p + 9);
        } else if (c == 91 && starts_with(u"*]", p + 1)) {
          emit(T_EDGE_STATE, p, p + 3);
        } else if (c == 99 && kw(u"class")) {
          push(S_cls);
          emit(T_CLASS, p, p + 5);
        } else {
          ok = common(c);
        }
        break;

      case S_string:
        if (c == 34) {
          pop();
          p++;
        } else {
          emit(T_STR, p, until(u'"'));
        }
        break;

      case S_generic:
        if (c == 34) {
          push(S_string);
          p++;
        } else if (keyword(c)) {
          break;
        } else if (c == 126) {
          pop();
          p++;
        } else {
          emit(T_GENERICTYPE, p, until(u'~'));
        }
        break;

      case S_bqstring:
        if (c == 34) {
          push(S_string);
          p++;
        } else if (keyword(c)) {
          break;
        } else if (c == 126) {
          push(S_generic);
          p++;
        } else if (c == 96) {
          pop();
          p++;
        } else {
          emit(T_BQUOTE_STR, p, until(u'`'));
        }
        break;

      case S_callbackName:
        if (c == 40) {
          const std::ptrdiff_t q = skip_ws(p + 1);
          pop();
          if (code_at(q) == 41) {
            p = q + 1;
          } else {
            push(S_callbackArgs);
            p++;
          }
        } else {
          emit(T_CALLBACK_NAME, p, until(u'('));
        }
        break;

      case S_callbackArgs:
        if (c == 41) {
          pop();
          p++;
        } else {
          emit(T_CALLBACK_ARGS, p, until(u')'));
        }
        break;

      case S_accTitle:
      case S_accDescr:
        emit(st == S_accTitle ? T_acc_title_value : T_acc_descr_value, p, until(u'\n'));
        pop();
        break;

      case S_accDescrMultiline:
        if (c == 125) {
          pop();
          p++;
        } else {
          emit(T_acc_descr_multiline_value, p, until(u'}'));
        }
        break;
    }

    if (!ok) {
      out.types.push_back(T_ERROR);
      out.texts.push_back(ustring());
      out.starts.push_back(static_cast<std::size_t>(p));
      return out;
    }
  }

  out.types.push_back(T_END);
  out.texts.push_back(ustring());
  out.starts.push_back(static_cast<std::size_t>(n));
  return out;
}

}  // namespace pele::classdiagram
