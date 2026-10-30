#include "er/er.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>

#include "chars.h"
#include "cluster.h"
#include "common/generics.h"
#include "compound.h"
#include "edges.h"
#include "entities.h"
#include "errors.h"
#include "fit_width.h"
#include "ink.h"
#include "js_format.h"
#include "js_math.h"
#include "label.h"
#include "layered.h"
#include "regex.h"
#include "svg.h"
#include "svg_document.h"
#include "theme.h"
#include "url.h"

namespace pele::er {
namespace {

// ---------------------------------------------------------------------------------------------
// Lexer

// The order Mermaid's generated parser numbers the tokens in.
enum token : int {
  T_END, T_ERROR, T_ER_DIAGRAM, T_EOF, T_NEWLINE, T_COLON, T_STYLE_SEPARATOR, T_BLOCK_START, T_BLOCK_STOP, T_SQS, T_SQE, T_acc_title, T_acc_title_value,
  T_acc_descr, T_acc_descr_value, T_acc_descr_multiline_value, T_END_KW, T_SUBGRAPH, T_direction_tb, T_direction_bt, T_direction_rl, T_direction_lr,
  T_CLASSDEF, T_UNICODE_TEXT, T_STYLE_TEXT, T_COMMA, T_CLASS, T_STYLE, T_SEMI, T_NUM, T_BRKT, T_ENTITY_NAME, T_DECIMAL_NUM, T_ENTITY_ONE, T_ATTRIBUTE_WORD,
  T_ATTRIBUTE_KEY, T_COMMENT, T_ZERO_OR_ONE, T_ZERO_OR_MORE, T_ONE_OR_MORE, T_ONLY_ONE, T_MD_PARENT, T_NON_IDENTIFYING, T_IDENTIFYING, T_WORD,
  // A character no rule names; the grammar gives it meaning only as `?` and `,` inside a block.
  T_CHAR
};

const char* const kTokenNames[] = {"$end", "INVALID", "ER_DIAGRAM", "EOF", "NEWLINE", "COLON", "STYLE_SEPARATOR", "BLOCK_START", "BLOCK_STOP", "SQS", "SQE",
                                   "acc_title", "acc_title_value", "acc_descr", "acc_descr_value", "acc_descr_multiline_value", "END", "SUBGRAPH",
                                   "direction_tb", "direction_bt", "direction_rl", "direction_lr", "CLASSDEF", "UNICODE_TEXT", "STYLE_TEXT", "COMMA", "CLASS",
                                   "STYLE", "SEMI", "NUM", "BRKT", "ENTITY_NAME", "DECIMAL_NUM", "ENTITY_ONE", "ATTRIBUTE_WORD", "ATTRIBUTE_KEY", "COMMENT",
                                   "ZERO_OR_ONE", "ZERO_OR_MORE", "ONE_OR_MORE", "ONLY_ONE", "MD_PARENT", "NON_IDENTIFYING", "IDENTIFYING", "WORD", "CHAR"};

enum lex_state : int { S_INITIAL, S_block, S_block_bq, S_acc_title, S_acc_descr, S_acc_descr_multiline, S_style };

constexpr unsigned char kText = 1;
constexpr unsigned char kStyleText = 2;
constexpr unsigned char kAttrStart = 4;
constexpr unsigned char kAttrRest = 8;

const std::array<unsigned char, 128>& char_table() {
  static const std::array<unsigned char, 128> table = [] {
    std::array<unsigned char, 128> t{};
    for (int c = 0; c < 128; c++) {
      const bool word = (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c == 95;
      const bool letter = word && (c < 48 || c > 57);
      if (word) t[static_cast<std::size_t>(c)] |= kText | kStyleText | kAttrRest;
      if (letter) t[static_cast<std::size_t>(c)] |= kAttrStart;
    }
    for (const char ch : std::string("-*")) t[static_cast<std::size_t>(ch)] |= kText | kStyleText | kAttrRest;
    t[46] |= kText | kAttrRest;
    t[42] |= kAttrStart;
    for (const char ch : std::string("[](),")) t[static_cast<std::size_t>(ch)] |= kAttrRest;
    return t;
  }();
  return table;
}

// The class bits of a code unit; none past the end (-1) or beyond ASCII.
int cls(int c) { return c >= 0 && c < 128 ? char_table()[static_cast<std::size_t>(c)] : 0; }

// Mermaid's attribute pattern is case-insensitive, which lets the micro sign into its \u00c0-\uffff range.
bool is_attr(int c, unsigned char kind) { return c < 128 ? (cls(c) & kind) != 0 : c >= 0xc0 || c == 0xb5; }

const js_regex& re_acc_title() {
  static const js_regex re("accTitle\\s*:\\s*", "i");
  return re;
}
const js_regex& re_acc_descr() {
  static const js_regex re("accDescr\\s*:\\s*", "i");
  return re;
}
const js_regex& re_acc_descr_ml() {
  static const js_regex re("accDescr\\s*\\{\\s*", "i");
  return re;
}
const js_regex& re_direction(int i) {
  static const js_regex res[] = {js_regex(".*direction\\s+TB[^\\n]*", "i"), js_regex(".*direction\\s+BT[^\\n]*", "i"), js_regex(".*direction\\s+RL[^\\n]*", "i"),
                                 js_regex(".*direction\\s+LR[^\\n]*", "i")};
  return res[i];
}

struct tokens {
  std::vector<int> types;
  std::vector<ustring> texts;
  std::vector<std::size_t> starts;
};

tokens tokenize(ustring_view src) {
  using idx = std::ptrdiff_t;
  const idx n = static_cast<idx>(src.size());
  tokens out;
  int state = S_INITIAL;
  idx p = 0;
  idx dir_next = -2;
  idx dir_failed = 0;
  idx line_end = -1;
  idx tilde_next = -2;
  idx tilde_line = -1;
  idx tilde_last = -1;
  idx ws_next = -1;

  const auto code = [&](idx i) -> int { return i >= 0 && i < n ? src[static_cast<std::size_t>(i)] : -1; };
  const auto emit = [&](int type, idx s, idx e) {
    out.types.push_back(type);
    out.texts.push_back(ustring(src.substr(static_cast<std::size_t>(s), static_cast<std::size_t>(e - s))));
    out.starts.push_back(static_cast<std::size_t>(s));
    p = e;
  };
  const auto at = [&](const js_regex& re, idx q) -> idx {
    const auto m = re.match_at(src, static_cast<std::size_t>(q));
    return m ? static_cast<idx>(m->end) : -1;
  };
  // Matches a lower-case keyword at p, ignoring case.
  const auto kw = [&](const char* word) {
    for (idx i = 0; word[i] != '\0'; i++) {
      const int k = static_cast<unsigned char>(word[i]);
      const int c = code(p + i);
      if (c != k && (k < 97 || k > 122 || (c | 32) != k)) return false;
    }
    return true;
  };
  const auto kwb = [&](const char* word) { return kw(word) && !is_word(code(p + static_cast<idx>(std::strlen(word)))); };
  const auto lower = [&](idx q) -> int {
    const int c = code(q);
    return c >= 65 && c <= 90 ? c + 32 : c;
  };
  const auto next_line_end = [&](idx q) -> idx {
    while (q < n && !is_line_end(code(q))) q++;
    return q;
  };
  const auto digits = [&](idx q) {
    while (is_digit(code(q))) q++;
    return q;
  };

  // A direction statement swallows its whole line, from wherever the lexer stands on it.
  const auto direction = [&]() -> bool {
    if (dir_next == -1) return false;
    if (dir_next < p) {
      // /direction/gi from p
      dir_next = -1;
      for (idx q = p; q + 9 <= n; q++) {
        bool same = true;
        for (idx k = 0; k < 9 && same; k++) {
          const int c = code(q + k);
          same = (c | 32) == "direction"[k] && ((c >= 65 && c <= 90) || (c >= 97 && c <= 122));
        }
        if (same) {
          dir_next = q;
          break;
        }
      }
      if (dir_next == -1) return false;
    }
    if (p < dir_failed) return false;
    if (line_end < p) line_end = next_line_end(p);
    if (dir_next >= line_end) return false;
    for (int i = 0; i < 4; i++) {
      const idx e = at(re_direction(i), p);
      if (e >= 0) {
        emit(T_direction_tb + i, p, e);
        return true;
      }
    }
    dir_failed = line_end;
    return false;
  };

  const auto quoted = [&]() {
    const std::size_t found = src.find(u'"', static_cast<std::size_t>(p + 1));
    if (found == ustring_view::npos) {
      emit(T_CHAR, p, p + 1);
      return;
    }
    const idx e = static_cast<idx>(found);
    bool name = e > p + 1;
    for (idx q = p + 1; name && q < e; q++) {
      const int c = code(q);
      if (c == 37 || c == 92 || c == 13 || c == 10 || c == 11 || c == 8) name = false;
    }
    emit(name ? T_ENTITY_NAME : T_WORD, p, e + 1);
  };

  const auto number = [&](int c) {
    const int d = code(p + 1);
    if (d == 43 && (c == 49 || c == 48)) {
      emit(c == 49 ? T_ONE_OR_MORE : T_ZERO_OR_MORE, p, p + 2);
      return;
    }
    const idx q = digits(p + 1);
    if (code(q) == 46 && is_digit(code(q + 1))) {
      emit(T_DECIMAL_NUM, p, digits(q + 2));
      return;
    }
    if (c == 49) {
      if (is_space(d)) {
        idx r = p + 2;
        while (is_space(code(r))) r++;
        const int f = code(r);
        if (is_word(f) || f == 34 || f == 39) {
          emit(T_ONLY_ONE, p, p + 1);
          return;
        }
      } else if (d == 45 || d == 46) {
        const int f = code(p + 2);
        if (f == 45 || f == 46) {
          emit(T_ONLY_ONE, p, p + 1);
          return;
        }
      }
      if (!is_word(d)) {
        emit(T_ENTITY_ONE, p, p + 1);
        return;
      }
    }
    emit(T_NUM, p, q);
  };

  const auto keyword = [&](int lc) -> bool {
    const int d = code(p + 1);
    switch (lc) {
      case 101:
        if (kwb("erdiagram")) {
          emit(T_ER_DIAGRAM, p, p + 9);
        } else if (kwb("end")) {
          idx e = p + 3;
          while (is_space(code(e))) e++;
          emit(T_END_KW, p, e);
        } else {
          return false;
        }
        return true;
      case 115:
        if (kwb("style")) {
          state = S_style;
          emit(T_STYLE, p, p + 5);
        } else if (kwb("subgraph")) {
          emit(T_SUBGRAPH, p, p + 8);
        } else {
          return false;
        }
        return true;
      case 99:
        if (kwb("classdef")) {
          state = S_style;
          emit(T_CLASSDEF, p, p + 8);
        } else if (kwb("class")) {
          emit(T_CLASS, p, p + 5);
        } else {
          return false;
        }
        return true;
      case 111:
        if (kwb("one or zero")) emit(T_ZERO_OR_ONE, p, p + 11);
        else if (kwb("one or more") || kwb("one or many")) emit(T_ONE_OR_MORE, p, p + 11);
        else if (kwb("one")) emit(T_ONLY_ONE, p, p + 3);
        else if (kwb("only one")) emit(T_ONLY_ONE, p, p + 8);
        else if (d == 124) emit(T_ZERO_OR_ONE, p, p + 2);
        else if (d == 123) emit(T_ZERO_OR_MORE, p, p + 2);
        else if (kwb("optionally to")) emit(T_NON_IDENTIFYING, p, p + 13);
        else return false;
        return true;
      case 122:
        if (kwb("zero or one")) emit(T_ZERO_OR_ONE, p, p + 11);
        else if (kwb("zero or more") || kwb("zero or many")) emit(T_ZERO_OR_MORE, p, p + 12);
        else return false;
        return true;
      case 109:
        if (kw("many(0)")) emit(T_ZERO_OR_MORE, p, p + 7);
        else if (kw("many(1)")) emit(T_ONE_OR_MORE, p, p + 7);
        else if (kwb("many")) emit(T_ZERO_OR_MORE, p, p + 4);
        else return false;
        return true;
      case 116:
        if (!kwb("to")) return false;
        emit(T_IDENTIFYING, p, p + 2);
        return true;
      case 117:
        if (d != 46 && d != 45 && d != 124) return false;
        emit(T_MD_PARENT, p, p + 1);
        return true;
      case 124:
        if (lower(p + 1) == 111 && !is_word(code(p + 2))) emit(T_ZERO_OR_ONE, p, p + 2);
        else if (d == 124) emit(T_ONLY_ONE, p, p + 2);
        else if (d == 123) emit(T_ONE_OR_MORE, p, p + 2);
        else return false;
        return true;
      case 125:
        if (lower(p + 1) == 111 && !is_word(code(p + 2))) emit(T_ZERO_OR_MORE, p, p + 2);
        else if (d == 124) emit(T_ONE_OR_MORE, p, p + 2);
        else return false;
        return true;
      case 46:
        if (d != 46 && d != 45) return false;
        emit(T_NON_IDENTIFYING, p, p + 2);
        return true;
      case 45:
        if (d == 45) emit(T_IDENTIFYING, p, p + 2);
        else if (d == 46) emit(T_NON_IDENTIFYING, p, p + 2);
        else return false;
        return true;
    }
    return false;
  };

  const auto initial = [&](int c) {
    idx e;
    if ((c == 97 || c == 65) && kw("acc")) {
      if ((e = at(re_acc_title(), p)) >= 0) {
        state = S_acc_title;
        emit(T_acc_title, p, e);
        return;
      }
      if ((e = at(re_acc_descr(), p)) >= 0) {
        state = S_acc_descr;
        emit(T_acc_descr, p, e);
        return;
      }
      if ((e = at(re_acc_descr_ml(), p)) >= 0) {
        state = S_acc_descr_multiline;
        p = e;
        return;
      }
    }
    if (direction()) return;

    switch (c) {
      case 32:
      case 9:
      case 13:
        e = p + 1;
        for (int d = code(e); d == 32 || d == 9 || d == 13; d = code(e)) e++;
        p = e;
        return;
      case 10:
        e = p + 1;
        while (code(e) == 10) e++;
        emit(T_NEWLINE, p, e);
        return;
      case 34:
        quoted();
        return;
      case 123:
        state = S_block;
        emit(T_BLOCK_START, p, p + 1);
        return;
      case 35:
        emit(T_BRKT, p, p + 1);
        return;
      case 44:
        emit(T_COMMA, p, p + 1);
        return;
      case 58:
        if (code(p + 1) == 58 && code(p + 2) == 58) emit(T_STYLE_SEPARATOR, p, p + 3);
        else emit(T_COLON, p, p + 1);
        return;
      case 91:
        emit(T_SQS, p, p + 1);
        return;
      case 93:
        emit(T_SQE, p, p + 1);
        return;
    }
    if (is_digit(c)) {
      number(c);
      return;
    }
    if (keyword(c >= 65 && c <= 90 ? c + 32 : c)) return;
    if (c >= 128 || (cls(c) & kText) != 0) {
      e = p + 1;
      for (int d = code(e); d >= 128 || (cls(d) & kText) != 0; d = code(e)) e++;
      emit(T_UNICODE_TEXT, p, e);
      return;
    }
    emit(T_CHAR, p, p + 1);
  };

  // Mermaid's generic type rule: a run of non-space characters holding a `~`, through the last `~` on the line.
  const auto generic = [&]() -> bool {
    if (tilde_next == -1) return false;
    if (tilde_next < p) {
      const std::size_t found = src.find(u'~', static_cast<std::size_t>(p));
      tilde_next = found == ustring_view::npos ? -1 : static_cast<idx>(found);
      if (tilde_next == -1) return false;
    }
    if (ws_next < p) {
      idx q = p;
      while (q < n && !is_space(code(q))) q++;
      ws_next = q;
    }
    if (tilde_next >= ws_next) return false;
    if (tilde_line < p) {
      tilde_line = next_line_end(p);
      // src.lastIndexOf('~', tildeLine - 1): a negative start counts as 0.
      const std::size_t from = static_cast<std::size_t>(std::max<idx>(tilde_line - 1, 0));
      const std::size_t found = src.rfind(u'~', from);
      tilde_last = found == ustring_view::npos ? -1 : static_cast<idx>(found);
    }
    if (tilde_last <= tilde_next) return false;
    idx e = tilde_last + 1;
    while (e < n && !is_space(code(e))) e++;
    emit(T_ATTRIBUTE_WORD, p, e);
    return true;
  };

  const auto block = [&](int c) {
    if (is_space(c)) {
      p++;
      while (is_space(code(p))) p++;
      return;
    }
    const int lc = c | 32;
    if ((lc == 112 || lc == 102 || lc == 117) && (code(p + 1) | 32) == 107 && !is_word(code(p + 2))) {
      emit(T_ATTRIBUTE_KEY, p, p + 2);
      return;
    }
    if (generic()) return;
    if (is_attr(c, kAttrStart)) {
      idx e = p + 1;
      while (e < n && is_attr(code(e), kAttrRest)) e++;
      emit(T_ATTRIBUTE_WORD, p, e);
      return;
    }
    if (c == 96) {
      state = S_block_bq;
      p++;
      return;
    }
    if (c == 34) {
      const std::size_t found = src.find(u'"', static_cast<std::size_t>(p + 1));
      if (found != ustring_view::npos) {
        emit(T_COMMENT, p, static_cast<idx>(found) + 1);
        return;
      }
    } else if (c == 125) {
      state = S_INITIAL;
      emit(T_BLOCK_STOP, p, p + 1);
      return;
    }
    emit(T_CHAR, p, p + 1);
  };

  while (p < n) {
    const int c = code(p);
    switch (state) {
      case S_INITIAL:
        initial(c);
        break;

      case S_block:
        block(c);
        break;

      case S_block_bq:
        if (c == 96) {
          state = S_block;
          p++;
        } else {
          const std::size_t found = src.find(u'`', static_cast<std::size_t>(p));
          emit(T_ATTRIBUTE_WORD, p, found == ustring_view::npos ? n : static_cast<idx>(found));
        }
        break;

      case S_acc_title:
      case S_acc_descr: {
        const std::size_t found = src.find(u'\n', static_cast<std::size_t>(p));
        emit(state == S_acc_title ? T_acc_title_value : T_acc_descr_value, p, found == ustring_view::npos ? n : static_cast<idx>(found));
        state = S_INITIAL;
        break;
      }

      case S_acc_descr_multiline:
        if (c == 125) {
          state = S_INITIAL;
          p++;
        } else {
          const std::size_t found = src.find(u'}', static_cast<std::size_t>(p));
          emit(T_acc_descr_multiline_value, p, found == ustring_view::npos ? n : static_cast<idx>(found));
        }
        break;

      case S_style:
        if (c == 10) {
          idx e = p + 1;
          while (code(e) == 10) e++;
          state = S_INITIAL;
          emit(T_NEWLINE, p, e);
        } else if (is_space(c)) {
          p++;
          while (is_space(code(p))) p++;
        } else if (c == 58) {
          emit(T_COLON, p, p + 1);
        } else if (c == 44) {
          emit(T_COMMA, p, p + 1);
        } else if (c == 35) {
          emit(T_BRKT, p, p + 1);
        } else if (c == 59) {
          emit(T_SEMI, p, p + 1);
        } else if (c >= 128 || (cls(c) & kStyleText) != 0) {
          idx e = p + 1;
          for (int d = code(e); d >= 128 || (cls(d) & kStyleText) != 0; d = code(e)) e++;
          emit(T_STYLE_TEXT, p, e);
        } else {
          out.types.push_back(T_ERROR);
          out.texts.push_back(ustring());
          out.starts.push_back(static_cast<std::size_t>(p));
          return out;
        }
        break;
    }
  }

  // At the end of input Jison's INITIAL state yields EOF, the title and description states yield
  // an empty value, and the block and style states yield nothing.
  const int last = state == S_INITIAL ? T_EOF : state == S_acc_title ? T_acc_title_value : state == S_acc_descr ? T_acc_descr_value
                 : state == S_acc_descr_multiline ? T_acc_descr_multiline_value : -1;
  if (last != -1) {
    out.types.push_back(last);
    out.texts.push_back(ustring());
    out.starts.push_back(static_cast<std::size_t>(n));
  }
  out.types.push_back(T_END);
  out.texts.push_back(ustring());
  out.starts.push_back(static_cast<std::size_t>(n));
  return out;
}

// ---------------------------------------------------------------------------------------------
// The model, as Mermaid's ErDb builds it

struct parsed_attribute {
  ustring type;
  ustring name;
  std::vector<ustring> keys;
  ustring comment;
};

struct doc_item {
  bool is_dir = false;
  ustring text;
};

class er_db {
 public:
  explicit er_db(er_model& m) : model_(m) {}

  int add_entity(const ustring& name, const ustring& alias = ustring()) {
    auto it = model_.entity_index.find(name);
    if (it == model_.entity_index.end()) {
      er_entity entity;
      entity.id = u"entity-" + name + u"-" + to_utf16(std::to_string(model_.entities.size()));
      entity.label = name;
      entity.alias = alias;
      const int index = static_cast<int>(model_.entities.size());
      model_.entities.push_back(std::move(entity));
      model_.entity_index[name] = index;
      return index;
    }
    er_entity& entity = model_.entities[static_cast<std::size_t>(it->second)];
    if (entity.alias.empty() && !alias.empty()) entity.alias = alias;
    return it->second;
  }

  void add_attributes(const ustring& name, std::vector<parsed_attribute> attributes) {
    er_entity& entity = model_.entities[static_cast<std::size_t>(add_entity(name))];
    for (parsed_attribute& a : attributes) entity.attributes.push_back({std::move(a.type), std::move(a.name), std::move(a.keys), std::move(a.comment)});
  }

  void add_relationship(const ustring& ent_a, const ustring& role_a, const ustring& ent_b, cardinality card_a, bool identifying, cardinality card_b) {
    er_relationship r;
    r.entity_a = model_.subgraph_index.count(ent_a) ? ent_a : model_.entities[static_cast<std::size_t>(add_entity(ent_a))].id;
    r.entity_b = model_.subgraph_index.count(ent_b) ? ent_b : model_.entities[static_cast<std::size_t>(add_entity(ent_b))].id;
    r.role_a = role_a;
    r.card_a = card_a;
    r.identifying = identifying;
    r.card_b = card_b;
    model_.relationships.push_back(std::move(r));
  }

  void set_direction(const ustring& dir) { model_.direction = dir; }

  void add_css_styles(const std::vector<ustring>& ids, const std::vector<ustring>& styles) {
    for (const ustring& id : ids) {
      const auto e = model_.entity_index.find(id);
      if (e != model_.entity_index.end())
        for (const ustring& s : styles) model_.entities[static_cast<std::size_t>(e->second)].css_styles.push_back(s);
      const auto g = model_.subgraph_index.find(id);
      if (g != model_.subgraph_index.end())
        for (const ustring& s : styles) model_.subgraphs[static_cast<std::size_t>(g->second)].css_styles.push_back(s);
    }
  }

  void add_class(const std::vector<ustring>& ids, const std::vector<ustring>& styles) {
    for (const ustring& id : ids) {
      auto it = model_.class_index.find(id);
      if (it == model_.class_index.end()) {
        model_.class_index[id] = static_cast<int>(model_.classes.size());
        model_.classes.push_back({id, {}, {}});
        it = model_.class_index.find(id);
      }
      er_class_def& def = model_.classes[static_cast<std::size_t>(it->second)];
      for (const ustring& style : styles) {
        if (style.find(u"color") != ustring::npos) {
          // style.replace('fill', 'bgFill'): the first one only.
          ustring text = style;
          const std::size_t at = text.find(u"fill");
          if (at != ustring::npos) text.replace(at, 4, u"bgFill");
          def.text_styles.push_back(std::move(text));
        }
        def.styles.push_back(style);
      }
    }
  }

  void set_class(const std::vector<ustring>& ids, const std::vector<ustring>& class_names) {
    for (const ustring& id : ids) {
      const auto e = model_.entity_index.find(id);
      if (e != model_.entity_index.end())
        for (const ustring& name : class_names) model_.entities[static_cast<std::size_t>(e->second)].css_classes += u" " + name;
      const auto g = model_.subgraph_index.find(id);
      if (g != model_.subgraph_index.end())
        for (const ustring& name : class_names) model_.subgraphs[static_cast<std::size_t>(g->second)].classes.push_back(name);
    }
  }

  ustring add_sub_graph(const ustring& id_text, const std::vector<doc_item>& list, const ustring& title_text) {
    const ustring id = trim(id_text);
    std::set<ustring> seen;
    std::vector<ustring> nodes;
    std::optional<ustring> dir;
    for (const doc_item& item : list) {
      if (item.is_dir) {
        dir = item.text;
        continue;
      }
      const ustring trimmed = trim(item.text);
      if (trimmed.empty() || seen.count(trimmed)) continue;
      seen.insert(trimmed);
      // A node belongs to the first subgraph that lists it.
      if (!claimed_.count(item.text)) nodes.push_back(item.text);
    }
    for (const ustring& node : nodes) claimed_.insert(node);
    er_subgraph subgraph;
    subgraph.id = id;
    subgraph.nodes = std::move(nodes);
    subgraph.title = trim(title_text);
    subgraph.dir = dir;
    // Setting the lookup after the list makes a repeated id point at the later subgraph.
    model_.subgraphs.push_back(std::move(subgraph));
    model_.subgraph_index[id] = static_cast<int>(model_.subgraphs.size()) - 1;
    return id;
  }

  void set_acc_title(const ustring& text) { model_.acc_title = trim_start(text); }

  void set_acc_description(const ustring& text) {
    // text.replace(/\n\s+/g, '\n')
    ustring out;
    for (std::size_t i = 0; i < text.size(); i++) {
      out += text[i];
      if (text[i] != u'\n') continue;
      while (i + 1 < text.size() && is_space(text[i + 1])) i++;
    }
    model_.acc_descr = out;
  }

 private:
  er_model& model_;
  std::set<ustring> claimed_;
};

// ---------------------------------------------------------------------------------------------
// The parser

const char* const kName = "'UNICODE_TEXT', 'NUM', 'ENTITY_NAME', 'DECIMAL_NUM', 'ENTITY_ONE'";
const char* const kCards = "'ZERO_OR_ONE', 'ZERO_OR_MORE', 'ONE_OR_MORE', 'ONLY_ONE', 'MD_PARENT'";
const std::string kLine = std::string("'EOF', 'NEWLINE', 'title', 'acc_title', 'acc_descr', 'acc_descr_multiline_value', 'SUBGRAPH', 'direction_tb', ") +
                          "'direction_bt', 'direction_rl', 'direction_lr', 'CLASSDEF', 'CLASS', 'STYLE', " + kName;

bool cardinality_of(int t, cardinality& out) {
  switch (t) {
    case T_ZERO_OR_ONE: out = cardinality::zero_or_one; return true;
    case T_ZERO_OR_MORE: out = cardinality::zero_or_more; return true;
    case T_ONE_OR_MORE: out = cardinality::one_or_more; return true;
    case T_ONLY_ONE: out = cardinality::only_one; return true;
    case T_MD_PARENT: out = cardinality::md_parent; return true;
  }
  return false;
}

struct frame {
  ustring id;
  ustring title;
  // What Mermaid's grammar hands to addSubGraph: the value of every statement inside. Besides entity
  // names that includes accessibility text and the keywords `style`, `class` and `classDef`.
  std::vector<doc_item> list;
};

void parse(const ustring& src, er_model& model) {
  const tokens tk = tokenize(src);
  const std::vector<int>& types = tk.types;
  const std::vector<ustring>& texts = tk.texts;
  std::size_t i = 0;
  er_db db(model);

  const auto type_at = [&](std::size_t k) { return k < types.size() ? types[k] : -1; };
  const auto fail = [&](const std::string& expected) {
    const std::size_t at = std::min(i, tk.starts.size() - 1);
    const int t = type_at(i);
    if (t == T_ERROR) throw syntax_error("er", src, tk.starts[at], u"", true);
    const std::string got = t == T_CHAR ? to_utf8(texts[at]) : kTokenNames[t];
    throw syntax_error("er", src, tk.starts[at], to_utf16("Expecting " + expected + ", got '" + got + "'"));
  };
  const auto expect = [&](int type) -> ustring {
    if (type_at(i) != type) fail(std::string("'") + kTokenNames[type] + "'");
    return texts[i++];
  };
  const auto separator = [&]() {
    const int t = type_at(i);
    if (t != T_NEWLINE && t != T_SEMI && t != T_EOF) fail("'EOF', 'NEWLINE', 'SEMI'");
    i++;
  };
  const auto is_name = [](int t) { return t == T_UNICODE_TEXT || t == T_ENTITY_NAME || t == T_NUM || t == T_DECIMAL_NUM || t == T_ENTITY_ONE; };
  const auto entity_name = [&]() -> ustring {
    const int t = type_at(i);
    if (!is_name(t)) fail(kName);
    const ustring& text = texts[i++];
    return t == T_ENTITY_NAME ? text.substr(1, text.size() - 2) : text;
  };
  const auto id_list = [&]() {
    std::vector<ustring> ids;
    for (;;) {
      const int t = type_at(i);
      if (t != T_UNICODE_TEXT && t != T_STYLE_TEXT) fail("'UNICODE_TEXT', 'STYLE_TEXT'");
      ids.push_back(texts[i++]);
      if (type_at(i) != T_COMMA) return ids;
      i++;
    }
  };
  const auto is_style_component = [](int t) { return t == T_STYLE_TEXT || t == T_NUM || t == T_COLON || t == T_BRKT; };
  const auto styles_opt = [&]() {
    std::vector<ustring> styles;
    for (;;) {
      if (!is_style_component(type_at(i))) fail("'STYLE_TEXT', 'COLON', 'NUM', 'BRKT'");
      ustring style = texts[i++];
      while (is_style_component(type_at(i))) style += texts[i++];
      styles.push_back(std::move(style));
      if (type_at(i) != T_COMMA) return styles;
      i++;
    }
  };

  struct rel_spec {
    cardinality card_a = cardinality::only_one;
    bool identifying = false;
    cardinality card_b = cardinality::only_one;
  };
  const auto relation_spec = [&]() {
    rel_spec spec;
    cardinality_of(types[i++], spec.card_b);
    const int r = type_at(i);
    if (r != T_IDENTIFYING && r != T_NON_IDENTIFYING) fail("'NON_IDENTIFYING', 'IDENTIFYING'");
    spec.identifying = r == T_IDENTIFYING;
    i++;
    if (!cardinality_of(type_at(i), spec.card_a)) fail(kCards);
    i++;
    return spec;
  };
  const auto role = [&]() -> ustring {
    const int t = type_at(i);
    if (t != T_WORD && t != T_ENTITY_NAME && t != T_UNICODE_TEXT) fail("'UNICODE_TEXT', 'ENTITY_NAME', 'WORD'");
    const ustring& text = texts[i++];
    return t == T_UNICODE_TEXT ? text : text.substr(1, text.size() - 2);
  };
  const auto is_char = [&](char16_t ch) { return type_at(i) == T_CHAR && texts[i] == ustring(1, ch); };

  const auto attributes = [&]() {
    std::vector<parsed_attribute> list;
    do {
      parsed_attribute attribute;
      ustring type = expect(T_ATTRIBUTE_WORD);
      if (is_char(u'?')) type += texts[i++];
      if (type_at(i) != T_ATTRIBUTE_WORD) fail(!type.empty() && type.back() == u'?' ? "'ATTRIBUTE_WORD'" : "'ATTRIBUTE_WORD', '?'");
      attribute.type = type;
      attribute.name = texts[i++];
      if (type_at(i) == T_ATTRIBUTE_KEY) {
        attribute.keys.push_back(texts[i++]);
        while (is_char(u',')) {
          i++;
          attribute.keys.push_back(expect(T_ATTRIBUTE_KEY));
        }
      }
      if (type_at(i) == T_COMMENT) {
        const ustring& c = texts[i++];
        attribute.comment = c.substr(1, c.size() - 2);
      }
      list.push_back(std::move(attribute));
    } while (type_at(i) != T_BLOCK_STOP);
    return list;
  };

  // Parses an optional `{ ... }` block; none when there is none or it is empty.
  const auto block = [&]() -> std::optional<std::vector<parsed_attribute>> {
    if (type_at(i) != T_BLOCK_START) return std::nullopt;
    i++;
    if (type_at(i) == T_BLOCK_STOP) {
      i++;
      return std::nullopt;
    }
    std::vector<parsed_attribute> list = attributes();
    i++;
    return list;
  };

  const auto entity_statement = [&]() -> std::vector<ustring> {
    const ustring a = entity_name();
    int t = type_at(i);
    if (t == T_SQS) {
      i++;
      const ustring alias = entity_name();
      expect(T_SQE);
      std::optional<std::vector<ustring>> classes;
      if (type_at(i) == T_STYLE_SEPARATOR) {
        i++;
        classes = id_list();
      }
      auto attrs = block();
      db.add_entity(a, alias);
      if (attrs) db.add_attributes(a, std::move(*attrs));
      if (classes) db.set_class({a}, *classes);
      return {a};
    }
    std::optional<std::vector<ustring>> classes;
    if (t == T_STYLE_SEPARATOR) {
      i++;
      classes = id_list();
      t = type_at(i);
    }
    cardinality ignored;
    if (cardinality_of(t, ignored)) {
      const rel_spec spec = relation_spec();
      const ustring b = entity_name();
      std::optional<std::vector<ustring>> classes_b;
      if (type_at(i) == T_STYLE_SEPARATOR) {
        i++;
        classes_b = id_list();
      }
      expect(T_COLON);
      const ustring label = role();
      db.add_entity(a);
      db.add_entity(b);
      db.add_relationship(a, label, b, spec.card_a, spec.identifying, spec.card_b);
      if (classes) db.set_class({a}, *classes);
      if (classes_b) db.set_class({b}, *classes_b);
      return {a, b};
    }
    auto attrs = block();
    db.add_entity(a);
    if (attrs) db.add_attributes(a, std::move(*attrs));
    if (classes) db.set_class({a}, *classes);
    return {a};
  };

  expect(T_ER_DIAGRAM);
  // Subgraphs nest on this stack rather than on the call stack; the first frame is the diagram itself.
  std::vector<frame> frames(1);
  bool nested = false;
  static const char16_t* const directions[] = {u"TB", u"BT", u"RL", u"LR"};

  for (;;) {
    const int t = type_at(i);
    switch (t) {
      case T_NEWLINE:
        i++;
        break;
      case T_EOF:
        i++;
        if (nested) fail("'END', " + kLine);
        return;
      case T_END_KW: {
        if (!nested) fail(kLine);
        i++;
        const ustring id = db.add_sub_graph(frames.back().id, frames.back().list, frames.back().title);
        frames.pop_back();
        nested = frames.size() > 1;
        if (nested) frames.back().list.push_back({false, id});
        break;
      }
      case T_SUBGRAPH: {
        i++;
        const ustring id = entity_name();
        ustring title = id;
        if (type_at(i) == T_SQS) {
          i++;
          title = entity_name();
          while (is_name(type_at(i))) title += u" " + entity_name();
          expect(T_SQE);
        }
        separator();
        frames.push_back({id, title, {}});
        nested = true;
        break;
      }
      case T_acc_title: {
        i++;
        const ustring value = trim(expect(T_acc_title_value));
        db.set_acc_title(value);
        if (nested) frames.back().list.push_back({false, value});
        break;
      }
      case T_acc_descr: {
        i++;
        const ustring value = trim(expect(T_acc_descr_value));
        db.set_acc_description(value);
        if (nested) frames.back().list.push_back({false, value});
        break;
      }
      case T_acc_descr_multiline_value: {
        const ustring value = trim(texts[i++]);
        db.set_acc_description(value);
        if (nested) frames.back().list.push_back({false, value});
        break;
      }
      case T_direction_tb:
      case T_direction_bt:
      case T_direction_rl:
      case T_direction_lr: {
        i++;
        const ustring value = directions[t - T_direction_tb];
        if (nested) frames.back().list.push_back({true, value});
        else db.set_direction(value);
        break;
      }
      case T_CLASSDEF: {
        const ustring keyword = texts[i++];
        const std::vector<ustring> ids = id_list();
        const std::vector<ustring> styles = styles_opt();
        separator();
        db.add_class(ids, styles);
        if (nested) frames.back().list.push_back({false, keyword});
        break;
      }
      case T_CLASS: {
        const ustring keyword = texts[i++];
        const std::vector<ustring> ids = id_list();
        const std::vector<ustring> names = id_list();
        db.set_class(ids, names);
        if (nested) frames.back().list.push_back({false, keyword});
        break;
      }
      case T_STYLE: {
        const ustring keyword = texts[i++];
        const std::vector<ustring> ids = id_list();
        const std::vector<ustring> styles = styles_opt();
        separator();
        db.add_css_styles(ids, styles);
        if (nested) frames.back().list.push_back({false, keyword});
        break;
      }
      default: {
        if (!is_name(t)) fail(nested ? "'END', " + kLine : kLine);
        const std::vector<ustring> names = entity_statement();
        if (nested)
          for (const ustring& name : names) frames.back().list.push_back({false, name});
      }
    }
  }
}

}  // namespace

void parse_er(const ustring& source, er_model& model) { parse(source, model); }

namespace {

// ---------------------------------------------------------------------------------------------
// What is drawn: subgraphs as groups, entities as nodes with their class styles, and relationships
// as edges with a marker type at each end.

struct graph_node {
  ustring id;
  ustring label;
  ustring alias;
  const std::vector<er_attribute>* attributes = nullptr;
  bool is_group = false;
  std::optional<ustring> parent_id;
  std::vector<ustring> css_styles;
  std::vector<ustring> css_compiled;
  ustring css_classes;
  std::optional<ustring> dir;
};

struct graph_edge {
  ustring id;
  ustring start;
  ustring end;
  ustring label;
  const char* arrow_start = "";
  const char* arrow_end = "";
  bool dashed = false;
};

struct er_graph {
  std::vector<graph_node> nodes;
  std::vector<graph_edge> edges;
};

const char* marker_name(cardinality c) {
  switch (c) {
    case cardinality::zero_or_one: return "zero_or_one";
    case cardinality::zero_or_more: return "zero_or_more";
    case cardinality::one_or_more: return "one_or_more";
    case cardinality::only_one: return "only_one";
    case cardinality::md_parent: return "md_parent";
  }
  return "";
}

// `text.split(' ')`: empty pieces are kept.
std::vector<ustring> split_spaces(const ustring& text) {
  std::vector<ustring> out;
  std::size_t from = 0;
  for (;;) {
    const std::size_t at = text.find(u' ', from);
    if (at == ustring::npos) {
      out.push_back(text.substr(from));
      return out;
    }
    out.push_back(text.substr(from, at - from));
    from = at + 1;
  }
}

er_graph build_graph(const er_model& model) {
  er_graph graph;
  const auto compiled = [&](const std::vector<ustring>& class_names) {
    std::vector<ustring> out;
    for (const ustring& name : class_names) {
      const auto it = model.class_index.find(name);
      if (it == model.class_index.end()) continue;
      const er_class_def& def = model.classes[static_cast<std::size_t>(it->second)];
      for (const ustring& style : def.styles) out.push_back(trim(style));
      for (const ustring& style : def.text_styles) out.push_back(trim(style));
    }
    return out;
  };

  std::map<ustring, ustring> parent_of;
  std::set<ustring> subgraph_ids;
  for (std::size_t i = model.subgraphs.size(); i-- > 0;) {
    const er_subgraph& subgraph = model.subgraphs[i];
    subgraph_ids.insert(subgraph.id);
    for (const ustring& id : subgraph.nodes) parent_of[id] = subgraph.id;
  }
  const auto parent = [&](const ustring& id) -> std::optional<ustring> {
    const auto it = parent_of.find(id);
    return it == parent_of.end() ? std::nullopt : std::optional<ustring>(it->second);
  };

  for (std::size_t i = model.subgraphs.size(); i-- > 0;) {
    const er_subgraph& subgraph = model.subgraphs[i];
    graph_node node;
    node.id = subgraph.id;
    node.label = subgraph.title;
    node.is_group = true;
    node.parent_id = parent(subgraph.id);
    node.css_styles = subgraph.css_styles;
    node.css_compiled = compiled(subgraph.classes);
    for (std::size_t k = 0; k < subgraph.classes.size(); k++) node.css_classes += (k > 0 ? u" " : u"") + subgraph.classes[k];
    node.dir = subgraph.dir;
    graph.nodes.push_back(std::move(node));
  }

  // A name that is also a subgraph id stands for the subgraph. Mermaid drops such an entity and
  // leaves relationships written before the subgraph pointing at nothing; here they follow it.
  std::map<ustring, ustring> moved;
  for (const er_entity& entity : model.entities) {
    if (subgraph_ids.count(entity.label)) {
      moved[entity.id] = entity.label;
      continue;
    }
    graph_node node;
    node.id = entity.id;
    node.label = entity.label;
    node.alias = entity.alias;
    node.attributes = &entity.attributes;
    node.parent_id = parent(entity.label);
    node.css_styles = entity.css_styles;
    node.css_compiled = compiled(split_spaces(entity.css_classes));
    node.css_classes = entity.css_classes;
    graph.nodes.push_back(std::move(node));
  }

  const auto follow = [&](const ustring& id) {
    const auto it = moved.find(id);
    return it == moved.end() ? id : it->second;
  };
  for (std::size_t index = 0; index < model.relationships.size(); index++) {
    const er_relationship& r = model.relationships[index];
    graph_edge edge;
    edge.id = u"id_" + r.entity_a + u"_" + r.entity_b + u"_" + to_utf16(std::to_string(index));
    edge.start = follow(r.entity_a);
    edge.end = follow(r.entity_b);
    edge.label = r.role_a;
    edge.arrow_start = marker_name(r.card_b);
    edge.arrow_end = marker_name(r.card_a);
    edge.dashed = !r.identifying;
    graph.edges.push_back(std::move(edge));
  }
  return graph;
}

// ---------------------------------------------------------------------------------------------
// Cardinality markers

// How far a marker reaches back along its line from the box it touches.
constexpr double kMarkerLength = 18;
constexpr double kBar = 5;
constexpr double kFoot = 6;
constexpr double kFootLength = 10;
constexpr double kRing = 3.5;

using svg::esc;
using svg::esc_text;
using svg::num;

// Draws a crow's foot cardinality marker over the end of a line that meets a box at (x, y) while
// travelling along the unit vector (dx, dy).
std::string er_marker(const std::string& type, double x, double y, double dx, double dy) {
  const auto at = [&](double back, double side) { return num(x - dx * back - dy * side) + "," + num(y - dy * back + dx * side); };
  const auto bar = [&](double back) { return "M" + at(back, -kBar) + "L" + at(back, kBar); };
  const std::string foot = "M" + at(0, -kFoot) + "L" + at(kFootLength, 0) + "L" + at(0, kFoot);
  const auto ring = [&](double back) {
    return "<circle class=\"pele-marker\" cx=\"" + num(x - dx * back) + "\" cy=\"" + num(y - dy * back) + "\" r=\"" + num(kRing) + "\" fill=\"var(--_bg)\"/>";
  };
  const auto path = [](const std::string& d) { return "<path class=\"pele-marker\" d=\"" + d + "\"/>"; };

  if (type == "only_one") return path(bar(6) + bar(10));
  if (type == "zero_or_one") return path(bar(6)) + ring(13.5);
  if (type == "one_or_more") return path(foot + bar(kFootLength + 3.5));
  if (type == "zero_or_more") return path(foot) + ring(kFootLength + kRing + 0.5);
  if (type == "md_parent") {
    return "<path class=\"pele-marker\" d=\"M" + at(0, 0) + "L" + at(6, 4) + "L" + at(12, 0) + "L" + at(6, -4) + "Z\" fill=\"var(--_bg)\"/>";
  }
  return "";
}

// ---------------------------------------------------------------------------------------------
// Renderer

constexpr double kNodeSep = 48;
constexpr double kEdgeSep = 20;
constexpr double kRankSep = 64;
constexpr double kGroupPad = 20;
constexpr double kLoop = 28;
constexpr double kLoopBand = 38;
constexpr double kMinWidth = 96;
constexpr double kBoxPadX = 18;
constexpr double kBoxPadY = 12;
constexpr double kHeadPadY = 9;
constexpr double kCellPadX = 12;
constexpr double kCellGap = 16;
constexpr double kRowPad = 9;
constexpr double kCommentWrap = 260;
constexpr double kNoWrap = 1e6;
constexpr double kPortGap = 18;
constexpr double kPortMargin = 14;
const char* const kBoxAttrs = " fill=\"var(--_s)\" stroke=\"var(--_b)\"";

// A positive number set in the config, up to 2000, or the fallback.
double number_option(const yaml_value* section, const char* key, double fallback) {
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* value = section->find(key);
  return value != nullptr && value->is_number() && value->as_number() > 0 ? std::min(value->as_number(), 2000.0) : fallback;
}

ustring strip_default(const ustring& classes) {
  // /^default\s?/
  if (classes.compare(0, 7, u"default") != 0) return classes;
  std::size_t k = 7;
  if (k < classes.size() && is_space(classes[k])) k++;
  return classes.substr(k);
}

bool has_internal_link(const std::string& classes) {
  const std::string padded = classes + " ";
  const std::string needle = " internal-link";
  for (std::size_t at = padded.find(needle); at != std::string::npos; at = padded.find(needle, at + 1)) {
    const std::size_t end = at + needle.size();
    if (end >= padded.size() || padded[end] == ' ') return true;
  }
  return false;
}

// Whether a name may be written as the href of an internal link. The href keeps the original name,
// so sanitizing alone is not enough: the name is also checked as the browser and esc() will see it.
bool internal_link(const ustring& name, const render_options& options) {
  const ustring url = sanitize_url(name);
  if (url == u"about:blank" || !link_url(url, options)) return false;
  ustring seen;
  for (std::size_t i = 0; i < name.size(); i++) {
    const char16_t c = name[i];
    const bool pair = c >= 0xD800 && c <= 0xDBFF && i + 1 < name.size() && name[i + 1] >= 0xDC00 && name[i + 1] <= 0xDFFF;
    if (pair) {
      seen += c;
      seen += name[++i];
      continue;
    }
    const bool invalid = c <= 8 || c == 0x0b || c == 0x0c || (c >= 0x0e && c <= 0x1f) || c == 0x7f || (c >= 0xD800 && c <= 0xDFFF) || c == 0xfffe || c == 0xffff;
    if (invalid || c == u'\t' || c == u'\n' || c == u'\r') continue;
    seen += c;
  }
  std::size_t lead = 0;
  while (lead < seen.size() && seen[lead] <= 0x20) lead++;
  return link_url(seen.substr(lead), options).has_value();
}

bool font_family_in(const std::string& text) { return text.find("\"font-family:") != std::string::npos || text.find(";font-family:") != std::string::npos; }

// Type, name, keys, comment.
using cells = std::array<label, 4>;

struct row {
  cells text;
  double h = 0;
};

struct node_view {
  const graph_node* node = nullptr;
  theme::resolved_style style;
  label text;
  double w = 0;
  double h = 0;
  double head_h = 0;
  std::vector<row> rows;
  // Left edge of each column, measured from the left edge of the box.
  std::vector<double> cols;
  double row_size = 0;
  double key_size = 0;
  int mono = 0;
  // Whether self-relationships loop out of the bottom side rather than the right.
  bool below = false;
};

// A relationship from an entity to itself, with the stretch of the side it loops out of.
struct loop_info {
  label text;
  double band = 0;
  double at = 0;
};

// Writes a measured label starting at x, with the baseline of its first line at y.
std::string text_left(const label& l, double x, double y, const std::string& attrs, const icon_set& icons, int mono = 0) {
  const auto& lines = l.lines;
  if (lines.empty()) return "";
  const span* only = lines.size() == 1 && lines[0].size() == 1 ? &lines[0][0] : nullptr;
  // The short form collapses repeated spaces in monospace text.
  if (only != nullptr && !only->has_icon && only->style == mono && !(mono && only->text.find(u"  ") != ustring::npos)) {
    return "<text" + svg::apply_shared_style(attrs, mono) + " x=\"" + num(x) + "\" y=\"" + num(y) + "\">" + esc(only->text) + "</text>";
  }
  const int shared = svg::shared_style(l);
  const double shift = l.size * 0.35;
  const double top = y - shift - l.line_height / 2;
  std::string body;
  std::string extra;
  for (std::size_t i = 0; i < lines.size(); i++) {
    const auto& line = lines[i];
    const double middle = top + line_middle(l, i);
    y = middle + shift;
    bool has_icon = false;
    for (const span& s : line)
      if (s.has_icon) has_icon = true;
    double sx = x;
    bool placed = false;
    for (const span& s : line) {
      if (s.has_icon) {
        const double side = l.size * 1.1;
        extra += "<svg class=\"pele-icon\" data-icon=\"" + esc_text(s.icon) + "\" x=\"" + num(sx) + "\" y=\"" + num(y - l.size * 0.35 - side / 2) + "\" width=\"" + num(side) +
                 "\" height=\"" + num(side) +
                 "\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" + icons.markup(s.icon) + "</svg>";
      } else if (!s.text.empty()) {
        const std::string pos = has_icon || !placed ? " x=\"" + num(sx) + "\" y=\"" + num(y) + "\"" : "";
        body += "<tspan" + pos + svg::span_style(s.style & ~shared) + ">" + esc(s.text) + "</tspan>";
        placed = true;
      }
      sx += s.width;
    }
  }
  return "<text" + svg::apply_shared_style(attrs, shared) + " xml:space=\"preserve\">" + body + "</text>" + extra;
}

// Mermaid reads `~` pairs as angle brackets. They are written as entities so the label parser
// does not take `<int>` for a tag.
ustring cell_text(const ustring& raw) {
  const ustring converted = parse_generic_types(raw);
  if (converted == raw) return raw;
  ustring out;
  for (const char16_t c : converted) {
    if (c == u'<') out += u"&lt;";
    else if (c == u'>') out += u"&gt;";
    else out += c;
  }
  return out;
}

std::vector<double> straighten(const std::vector<double>& route) {
  std::vector<double> out(route.begin(), route.begin() + 3);
  for (std::size_t k = 3; k < route.size(); k += 3) {
    const std::size_t prev = out.size() - 3;
    const auto flat = [&](std::size_t axis) {
      return std::abs(out[prev + axis] - route[k + axis]) < 0.01 && std::abs(route[k + axis] - route[k + 3 + axis]) < 0.01;
    };
    if (k + 3 < route.size() && (flat(0) || flat(1))) continue;
    out.push_back(route[k]);
    out.push_back(route[k + 1]);
    out.push_back(route[k + 2]);
  }
  return out;
}

struct end_ref {
  std::vector<double>* route = nullptr;
  std::size_t at = 0;
  double toward = 0;
};

// The layout brings every edge that ends on one side of a box to the middle of that side, where
// their markers would cover each other. This spaces them along the side, in the order they fan out.
void spread_ends(std::vector<compound_edge>& cedges, const std::vector<compound_node>& cnodes, const std::vector<node_view>& views) {
  std::map<int, std::vector<end_ref>> sides;
  const auto add = [&](int node, std::vector<double>& route, std::size_t at, std::size_t next) {
    const compound_node& c = cnodes[static_cast<std::size_t>(node)];
    const double dx = route[at] - c.x;
    const double dy = route[at + 1] - c.y;
    int side;
    if (std::abs(std::abs(dy) - c.h / 2) < 0.5) side = dy < 0 ? 0 : 2;
    else if (std::abs(std::abs(dx) - c.w / 2) < 0.5) side = dx < 0 ? 3 : 1;
    else return;
    sides[node * 4 + side].push_back({&route, at, route[next + ((side & 1) ? 1 : 0)]});
  };
  for (compound_edge& e : cedges) {
    const long long last = static_cast<long long>(e.route.size()) - 3;
    if (e.src == e.dst || last < 3) continue;
    add(e.src, e.route, 0, 3);
    add(e.dst, e.route, static_cast<std::size_t>(last), static_cast<std::size_t>(last - 3));
  }
  for (auto& entry : sides) {
    std::vector<end_ref>& ends = entry.second;
    if (ends.size() < 2) continue;
    const int node = entry.first >> 2;
    const bool vertical = (entry.first & 1) == 1;
    const compound_node& c = cnodes[static_cast<std::size_t>(node)];
    const double box_w = c.is_group ? c.w : views[static_cast<std::size_t>(node)].w;
    const double box_h = c.is_group ? c.h : views[static_cast<std::size_t>(node)].h;
    const double length = vertical ? box_h : box_w;
    const double step = std::max(std::min(kPortGap, (length - 2 * kPortMargin) / static_cast<double>(ends.size() - 1)), 0.0);
    const double center = vertical ? c.y : c.x;
    std::stable_sort(ends.begin(), ends.end(), [](const end_ref& a, const end_ref& b) { return a.toward < b.toward; });
    for (std::size_t i = 0; i < ends.size(); i++) {
      (*ends[i].route)[ends[i].at + (vertical ? 1 : 0)] = center + (static_cast<double>(i) - static_cast<double>(ends.size() - 1) / 2) * step;
    }
  }
}

// An entity with a self-relationship is laid out larger than it is drawn; this moves a route end
// from the side of that larger box onto the entity's own side.
void inset(std::vector<double>& route, std::size_t at, const node_view& view, const compound_node& c) {
  if (c.is_group) return;
  const double dx = route[at] - c.x;
  const double dy = route[at + 1] - c.y;
  if (c.w != view.w && std::abs(std::abs(dx) - c.w / 2) < 0.5) route[at] -= (js_sign(dx) * (c.w - view.w)) / 2;
  if (c.h != view.h && std::abs(std::abs(dy) - c.h / 2) < 0.5) route[at + 1] -= (js_sign(dy) * (c.h - view.h)) / 2;
}

class er_renderer {
 public:
  er_renderer(const er_model& db, const config& settings, const render_options& options) : db_(db), settings_(settings), options_(options), icons_(options.icons) {}

  rendered render() {
    const std::string direction = to_utf8(db_.direction);
    if (!fit_width::runs_across(direction)) return draw(false);
    return fit_width::turn_to_fit(options_, [&](bool down) { return draw(down); });
  }

 private:
  // `turned` draws a diagram that runs across as one that runs down.
  rendered draw(bool turned) {
    const er_graph graph = build_graph(db_);
    const bool monospaced = std::find(options_.monospace.begin(), options_.monospace.end(), "attributes") != options_.monospace.end();
    const double size = options_.font_size ? *options_.font_size : 16;
    const double small_size = js_round(size * 0.875);
    const metrics_measurer default_measurer;
    const text_measurer& measurer = options_.measurer != nullptr ? *options_.measurer : default_measurer;
    const label_layouter layouter(measurer, &icons_);
    const double pad = options_.padding ? *options_.padding : 8;
    const yaml_value* conf = settings_.find("er");
    std::vector<rendered_link> links;

    std::map<ustring, std::size_t> index;
    std::vector<node_view> views;
    std::vector<compound_node> cnodes;
    for (std::size_t i = 0; i < graph.nodes.size(); i++) index[graph.nodes[i].id] = i;

    std::vector<compound_edge> cedges;
    std::vector<label> edge_labels;
    std::vector<const graph_edge*> drawn;
    std::vector<loop_info> loop_store;
    std::map<std::size_t, std::vector<std::size_t>> loops;
    std::vector<int> loop_of;
    for (const graph_edge& edge : graph.edges) {
      const auto src = index.find(edge.start);
      const auto dst = index.find(edge.end);
      if (src == index.end() || dst == index.end()) continue;
      const label text = layouter.layout(edge.label, true, small_size, 200);
      int loop = -1;
      if (src->second == dst->second) {
        loop = static_cast<int>(loop_store.size());
        loop_store.push_back({text, 0, 0});
        loops[src->second].push_back(static_cast<std::size_t>(loop));
      }
      loop_of.push_back(loop);
      drawn.push_back(&edge);
      edge_labels.push_back(text);
      compound_edge ce;
      ce.src = static_cast<int>(src->second);
      ce.dst = static_cast<int>(dst->second);
      ce.minlen = 1;
      ce.label_w = text.width > 0 ? text.width + 12 : 0;
      ce.label_h = text.height > 0 ? text.height + 4 : 0;
      cedges.push_back(std::move(ce));
    }

    // The direction of the level a node is laid out in: its nearest subgraph that sets one, or the diagram's.
    const auto horizontal = [&](std::size_t i) {
      std::size_t at = i;
      // Subgraphs can name each other as members, so the walk up is bounded.
      for (std::size_t steps = 0; steps < graph.nodes.size(); steps++) {
        const auto& parent = graph.nodes[at].parent_id;
        const auto up = parent ? index.find(*parent) : index.end();
        if (up == index.end()) break;
        const auto& dir = graph.nodes[up->second].dir;
        if (dir && !dir->empty()) return *dir == u"LR" || *dir == u"RL";
        at = up->second;
      }
      return db_.direction == u"LR" || db_.direction == u"RL";
    };

    for (std::size_t i = 0; i < graph.nodes.size(); i++) {
      const graph_node& node = graph.nodes[i];
      std::vector<ustring> declarations = node.css_compiled;
      if (!node.css_styles.empty()) declarations.insert(declarations.end(), node.css_styles.begin(), node.css_styles.end());
      node_view view;
      view.node = &node;
      view.style = theme::resolve_style(declarations);
      const theme::resolved_style& style = view.style;
      const int base = (style.bold ? text_style::bold : 0) | (style.italic ? text_style::italic : 0);
      view.mono = monospaced && !font_family_in(style.text) ? text_style::mono : 0;
      const double name_size = style.font_size ? *style.font_size : size;
      view.row_size = style.font_size ? *style.font_size : small_size;
      view.key_size = style.font_size ? *style.font_size : js_round(size * 0.75);
      const ustring& name = node.alias.empty() ? node.label : node.alias;
      view.text = node.is_group ? layouter.layout(node.label, true, name_size, 4000, base) : layouter.layout(name, true, name_size, 240, base);
      const std::vector<std::size_t> none;
      const auto found = loops.find(i);
      const std::vector<std::size_t>& own = found == loops.end() ? none : found->second;
      // Loops leave by a side the layout keeps free of other edges: the right when ranks run down,
      // the bottom when they run across.
      view.below = !own.empty() && !node.is_group && horizontal(i);
      double reach = 0;
      double bands = 0;
      for (const std::size_t k : own) {
        loop_info& loop = loop_store[k];
        loop.band = std::max(kLoopBand, view.below ? loop.text.width + 12 : loop.text.height + 6);
        bands += loop.band;
        reach = std::max(reach, view.below ? loop.text.height : loop.text.width);
      }
      const double min_w = std::max(kMinWidth, view.below ? bands + 8 : 0);
      compound_node c;
      if (node.is_group) {
        c = compound_node(0, 0);
        c.is_group = true;
        if (node.dir && !node.dir->empty()) {
          c.has_dir = true;
          c.dir = parse_direction(to_utf8(*node.dir));
        }
        c.pad_x = kGroupPad;
        c.pad_top = view.text.height > 0 ? view.text.height + 16 : kGroupPad;
        c.pad_bottom = kGroupPad;
        c.min_w = std::max(view.text.width + 2 * kGroupPad - 12, kMinWidth);
      } else {
        const label& text = view.text;
        const double name_h = std::max(text.height, js_round(name_size * 1.5));
        if (node.attributes->empty()) {
          view.w = std::max(text.width + 2 * kBoxPadX, min_w);
          view.h = std::max(name_h + 2 * kBoxPadY, view.below ? 0 : bands + 8);
        } else {
          const double row_line = js_round(view.row_size * 1.5);
          double widths[4] = {0, 0, 0, 0};
          for (const er_attribute& attribute : *node.attributes) {
            row r;
            ustring keys;
            for (std::size_t k = 0; k < attribute.keys.size(); k++) keys += (k > 0 ? u", " : u"") + attribute.keys[k];
            r.text[0] = layouter.layout(cell_text(attribute.type), true, view.row_size, kNoWrap, base | view.mono);
            r.text[1] = layouter.layout(cell_text(attribute.name), true, view.row_size, kNoWrap, base | view.mono);
            r.text[2] = layouter.layout(keys, false, view.key_size, kNoWrap, base | view.mono);
            r.text[3] = layouter.layout(cell_text(attribute.comment), true, view.row_size, kCommentWrap, base);
            double h = row_line;
            for (std::size_t k = 0; k < 4; k++) {
              if (r.text[k].width > widths[k]) widths[k] = r.text[k].width;
              if (r.text[k].height > h) h = r.text[k].height;
            }
            r.h = h + kRowPad;
            view.h += h + kRowPad;
            view.rows.push_back(std::move(r));
          }
          int columns = 0;
          double inner = 0;
          for (const double w : widths) {
            if (w == 0) continue;
            inner += w + (columns > 0 ? kCellGap : 0);
            columns++;
          }
          view.w = std::max(std::max(inner + 2 * kCellPadX, text.width + 2 * kBoxPadX), min_w);
          // A box wider than its table shares the spare width between the columns.
          const double spare = columns > 0 ? (view.w - inner - 2 * kCellPadX) / columns : 0;
          double x = kCellPadX;
          for (const double w : widths) {
            view.cols.push_back(x);
            if (w > 0) x += w + spare + kCellGap;
          }
          view.head_h = name_h + 2 * kHeadPadY;
          view.h += view.head_h;
        }
        // The layout keeps a node centered in its box, so room for loops is added on both sides.
        const double room = !own.empty() ? 2 * (kMarkerLength + kLoop * 0.75 + 10 + reach) : 0;
        c = view.below ? compound_node(view.w, view.h + room) : compound_node(view.w + room, view.h);
      }
      c.seq = static_cast<double>(views.size());
      views.push_back(std::move(view));
      cnodes.push_back(c);
    }
    for (std::size_t i = 0; i < graph.nodes.size(); i++) {
      const auto& parent = graph.nodes[i].parent_id;
      const auto found = parent ? index.find(*parent) : index.end();
      cnodes[i].parent = parent ? (found != index.end() ? static_cast<int>(found->second) : -1) : -1;
    }

    layered_options layout_options;
    layout_options.node_sep = number_option(conf, "nodeSpacing", kNodeSep);
    layout_options.edge_sep = kEdgeSep;
    layout_options.rank_sep = number_option(conf, "rankSpacing", kRankSep);
    layout_options.port_sep = 24;
    const layered_layouter engine(layout_options);
    const compound_result layout = compound_layouter(engine).layout(cnodes, cedges, turned ? flow_dir::tb : parse_direction(to_utf8(db_.direction)));

    spread_ends(cedges, cnodes, views);

    // A subgraph gets its size from the layout, so its loops hang outside it and may widen the diagram.
    double layout_width = layout.width;
    for (const auto& entry : loops) {
      const compound_node& c = cnodes[entry.first];
      if (!c.is_group) continue;
      double reach = 0;
      for (const std::size_t k : entry.second) reach = std::max(reach, loop_store[k].text.width);
      layout_width = std::max(layout_width, c.x + c.w / 2 + kMarkerLength + kLoop * 0.75 + 10 + reach);
    }

    const label title = layouter.layout(db_.title ? ustring_view(*db_.title) : ustring_view(), false, size, 4000, text_style::bold);
    const double title_height = title.height > 0 ? title.height + 12 : 0;
    const double inner = std::max(layout_width, title.width);
    const double ox = pad + (inner - layout_width) / 2;
    const double oy = pad + title_height;
    compound_layouter::shift(cnodes, cedges, ox, oy);
    const double width = std::ceil(inner + 2 * pad);
    const double height = std::ceil(layout.height + title_height + 2 * pad);

    crossing_map crossings;
    bool any_group = false;
    for (const compound_node& c : cnodes)
      if (c.is_group) any_group = true;
    if (any_group)
      for (const compound_edge& e : cedges) crossings.mark(e.route);

    std::string clusters;
    std::vector<edge_geometry::fill> fills;
    std::string nodes_out;
    for (std::size_t i = 0; i < views.size(); i++) {
      node_view& view = views[i];
      const compound_node& c = cnodes[i];
      const graph_node& node = *view.node;
      const std::string classes = theme::class_names(strip_default(node.css_classes));
      if (c.is_group) {
        view.w = c.w;
        view.h = c.h;
        const double x = c.x - c.w / 2;
        const double y = c.y - c.h / 2;
        const double title_x = crossings.title_x(c, view.text.width, kGroupPad);
        if (paints(view.style.shape)) fills.push_back({{x, y, x + c.w, y + c.h}, view.style.shape});
        clusters += "<g class=\"pele-cluster" + classes + "\" data-id=\"" + esc_text(node.id) + "\"><rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(c.w) + "\" height=\"" +
                    num(c.h) + "\" fill=\"var(--_a)\"" + (theme::sets_fill(view.style.shape) ? "" : " fill-opacity=\"0.5\"") + " stroke=\"var(--_b)\"" + theme::rounded(view.style.shape) + "/>" +
                    svg::label_svg(view.text, title_x, y + 8 + view.text.height / 2, " class=\"pele-cluster-label\"" + ink_fill(view.style, "var(--_m)") + view.style.text, &icons_) + "</g>";
        continue;
      }

      const double w = view.w;
      const double h = view.h;
      const double x0 = -w / 2;
      const double y0 = -h / 2;
      const std::string& text = view.style.text;
      const std::string color = ink_fill(view.style);
      const std::string muted = !color.empty() ? color : " fill=\"var(--_m)\"";
      const std::string shape = "x=\"" + num(x0) + "\" y=\"" + num(y0) + "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\"";
      std::string name = svg::label_svg(view.text, 0, !view.rows.empty() ? y0 + view.head_h / 2 : 0, " class=\"pele-label\"" + color + text, &icons_);
      const ustring display = decode_entities(node.alias.empty() ? node.label : node.alias);
      if (has_internal_link(classes) && !name.empty() && options_.links) {
        // The name ends up in an href, so it gets the same check as a URL.
        const bool safe = internal_link(display, options_);
        const std::string target = esc(display);
        name = "<a class=\"internal-link\"" + (safe ? " href=\"" + target + "\"" + rel_attr(options_) : std::string()) + " data-href=\"" + target + "\">" + name + "</a>";
        links.push_back({node.label, display, true});
      }

      std::string body;
      if (view.rows.empty()) {
        body = "<rect " + shape + theme::rounded(std::string(kBoxAttrs) + view.style.shape) + "/>" + name;
      } else {
        const double row_line = js_round(view.row_size * 1.5);
        const std::string line_color = view.style.stroke ? esc(*view.style.stroke) : "var(--_b)";
        std::string stripes;
        std::string cells_out;
        double y = y0 + view.head_h;
        for (std::size_t r = 0; r < view.rows.size(); r++) {
          const row& rw = view.rows[r];
          if (r % 2 == 0) {
            const std::string span_attrs = "x=\"" + num(x0) + "\" width=\"" + num(w) + "\"";
            if (r == view.rows.size() - 1) {
              // The last row follows the box's rounded bottom corners: a rounded rect, squared off on top.
              stripes += "<rect " + span_attrs + " y=\"" + num(y) + "\" height=\"" + num(rw.h) + "\"" + theme::kRounded + "/>" + "<rect " + span_attrs + " y=\"" + num(y) + "\" height=\"" +
                         num(rw.h / 2) + "\"/>";
            } else {
              stripes += "<rect " + span_attrs + " y=\"" + num(y) + "\" height=\"" + num(rw.h) + "\"/>";
            }
          }
          const double baseline = y + kRowPad / 2 + row_line / 2 + view.row_size * 0.35;
          const label& type = rw.text[0];
          const label& attribute = rw.text[1];
          const label& keys = rw.text[2];
          const label& comment = rw.text[3];
          cells_out += text_left(type, x0 + view.cols[0], baseline, " class=\"pele-er-type\"" + muted + text, icons_, view.mono);
          cells_out += text_left(attribute, x0 + view.cols[1], baseline, " class=\"pele-er-name\"" + color + text, icons_, view.mono);
          cells_out += text_left(keys, x0 + view.cols[2], baseline, " class=\"pele-er-keys\" font-size=\"" + js_number_string(view.key_size) + "\"" + muted + text, icons_, view.mono);
          cells_out += text_left(comment, x0 + view.cols[3], baseline, " class=\"pele-er-comment\"" + muted + text, icons_);
          y += rw.h;
        }
        const std::string divider_y = num(y0 + view.head_h);
        // Keep row shading light on light fills and dark on dark fills to preserve text contrast.
        const std::optional<std::string> tone = ink(view.style.shape);
        const std::string shade = tone && *tone == kOnLight ? kOnDark : tone && *tone == kOnDark ? kOnLight : "var(--_bg)";
        body = "<rect " + shape + theme::rounded(std::string(kBoxAttrs) + view.style.shape) + "/>" +
               // Over a fill the diagram chose, the shading is a tint of that fill. The opacity is on the
               // group because the two parts of the last row overlap.
               "<g class=\"pele-er-stripes\" fill=\"" + shade + "\"" + (view.style.shape.find("fill:") != std::string::npos ? " opacity=\"0.6\"" : "") + ">" + stripes + "</g>" +
               "<path class=\"pele-er-divider\" d=\"M" + num(x0) + "," + divider_y + "H" + num(x0 + w) + "\" stroke=\"" + line_color + "\"/>" + "<rect " + shape +
               " fill=\"none\" stroke=\"var(--_b)\"" + theme::rounded(view.style.line) + "/>" + name + "<g class=\"pele-er-attributes\" font-size=\"" + js_number_string(view.row_size) + "\">" +
               cells_out + "</g>";
      }
      nodes_out += "<g class=\"pele-node pele-entity" + classes + "\" data-id=\"" + esc_text(node.label) + "\" transform=\"translate(" + num(c.x) + "," + num(c.y) + ")\">" + body + "</g>";
    }

    // Self-relationships share the side they loop out of, squeezed together if it is too short for them.
    for (const auto& entry : loops) {
      const node_view& view = views[entry.first];
      double total = 0;
      for (const std::size_t k : entry.second) total += loop_store[k].band;
      const double scale = std::min(1.0, std::max((view.below ? view.w : view.h) - 8, 0.0) / total);
      double at = (-total * scale) / 2;
      for (const std::size_t k : entry.second) {
        loop_info& loop = loop_store[k];
        loop.band *= scale;
        loop.at = at + loop.band / 2;
        at += loop.band;
      }
    }

    std::string edges_out;
    std::string casings;
    std::string labels_out;
    for (std::size_t i = 0; i < cedges.size(); i++) {
      compound_edge& ce = cedges[i];
      const graph_edge& edge = *drawn[i];
      const label& text = edge_labels[i];
      std::string d;
      std::string start;
      std::string end;
      if (loop_of[i] >= 0) {
        const loop_info& loop = loop_store[static_cast<std::size_t>(loop_of[i])];
        const compound_node& c = cnodes[static_cast<std::size_t>(ce.src)];
        const node_view& view = views[static_cast<std::size_t>(ce.src)];
        const double spread = std::max(std::min(loop.band / 2 - 6, 13.0), 1.0);
        const double far = kMarkerLength + kLoop * 0.75 + 6;
        if (view.below) {
          const double x = c.x + loop.at;
          const double y = c.y + view.h / 2;
          const std::string turn = num(y + kMarkerLength);
          const std::string out = num(y + kMarkerLength + kLoop);
          d = "M" + num(x - spread) + "," + num(y) + "V" + turn + "C" + num(x - spread) + "," + out + " " + num(x + spread) + "," + out + " " + num(x + spread) + "," + turn + "V" + num(y);
          start = er_marker(edge.arrow_start, x - spread, y, 0, -1);
          end = er_marker(edge.arrow_end, x + spread, y, 0, -1);
          ce.label_x = x;
          ce.label_y = y + far + text.height / 2;
        } else {
          const double x = c.x + view.w / 2;
          const double y = c.y + loop.at;
          const std::string turn = num(x + kMarkerLength);
          const std::string out = num(x + kMarkerLength + kLoop);
          d = "M" + num(x) + "," + num(y - spread) + "H" + turn + "C" + out + "," + num(y - spread) + " " + out + "," + num(y + spread) + " " + turn + "," + num(y + spread) + "H" + num(x);
          start = er_marker(edge.arrow_start, x, y - spread, -1, 0);
          end = er_marker(edge.arrow_end, x, y + spread, -1, 0);
          ce.label_x = x + far + text.width / 2;
          ce.label_y = y;
        }
      } else {
        std::vector<double> route = straighten(ce.route);
        if (route.size() < 6) continue;
        inset(route, 0, views[static_cast<std::size_t>(ce.src)], cnodes[static_cast<std::size_t>(ce.src)]);
        inset(route, route.size() - 3, views[static_cast<std::size_t>(ce.dst)], cnodes[static_cast<std::size_t>(ce.dst)]);
        // A curve stops short of each box so that the marker sits on a straight run of line.
        const double dx = std::abs(route[route.size() - 3] - route[0]);
        const double dy = std::abs(route[route.size() - 2] - route[1]);
        const bool direct = route.size() == 6 && (dx < 0.01 || dy < 0.01 || js_hypot(dx, dy) <= 3 * kMarkerLength);
        const double stub = direct ? 0 : kMarkerLength;
        const edge_path path = edge_geometry::route_path(route, "", stub, stub);
        d = stub > 0 ? "M" + num(path.sx) + "," + num(path.sy) + "L" + path.d.substr(1) + "L" + num(path.ex) + "," + num(path.ey) : path.d;
        start = er_marker(edge.arrow_start, path.sx, path.sy, path.sdx, path.sdy);
        end = er_marker(edge.arrow_end, path.ex, path.ey, path.edx, path.edy);
      }
      const edge_geometry::fill_hold under = edge_geometry::fill_under(d, {}, fills);
      // `over` is unset off the fills, empty where the edge needs a casing, and the fill's ink otherwise.
      const bool has_over = under.crossing || under.holder != nullptr;
      const std::string over = under.holder != nullptr ? edge_geometry::edge_ink(*under.holder) : std::string();
      if (has_over && over.empty()) casings += edge_geometry::casing(d, start + end);
      if (under.holder != nullptr && !over.empty()) {
        start = edge_geometry::hollow_on(start, *under.holder);
        end = edge_geometry::hollow_on(end, *under.holder);
      }
      edges_out += std::string("<g class=\"pele-edge pele-er-") + (edge.dashed ? "non-identifying" : "identifying") + "\" data-id=\"" + esc_text(edge.id) + "\"" +
                   (!over.empty() ? " stroke=\"" + over + "\"" : "") + "><path d=\"" + d + "\"" + (edge.dashed ? " stroke-dasharray=\"4 3\"" : "") + "/>" + start + end + "</g>";
      if (text.width > 0) labels_out += edge_geometry::label_svg(to_utf8(edge.id), text, ce.label_x, ce.label_y, "", &icons_);
    }

    accessible acc;
    if (db_.acc_title) acc.title = *db_.acc_title;
    if (db_.acc_descr) acc.description = *db_.acc_descr;
    const std::string content = svg::label_svg(title, width / 2, pad + title.height / 2, " class=\"pele-title\" font-weight=\"var(--_tw)\"") +
                                (clusters.empty() ? "" : "<g class=\"pele-clusters\">" + clusters + "</g>") + edge_geometry::casings_svg(casings) +
                                (edges_out.empty() ? "" : "<g class=\"pele-edges\" fill=\"none\" stroke=\"var(--_l)\" stroke-linecap=\"round\">" + edges_out + "</g>") +
                                (labels_out.empty() ? "" : "<g class=\"pele-edge-labels\" font-size=\"" + js_number_string(small_size) + "\">" + labels_out + "</g>") +
                                "<g class=\"pele-nodes\">" + nodes_out + "</g>";
    rendered result;
    result.svg = svg_document::build("er", width, height, size, options_, acc, content);
    result.width = width;
    result.height = height;
    result.links = std::move(links);
    return result;
  }

  const er_model& db_;
  const config& settings_;
  const render_options& options_;
  icon_set icons_;
};

}  // namespace

rendered render_er(const er_model& model, const config& settings, const render_options& options) { return er_renderer(model, settings, options).render(); }

}  // namespace pele::er
