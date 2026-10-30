#include "state/state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>

#include "chars.h"
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
#include "shapes.h"
#include "svg.h"
#include "svg_document.h"
#include "theme.h"
#include "title.h"
#include "url.h"

namespace pele::state {
namespace {

// ---------------------------------------------------------------------------------------------
// Lexer

// The order Mermaid's generated parser numbers the tokens in.
enum token : int {
  T_END, T_ERROR, T_NL, T_SD, T_DESCR, T_ARROW, T_HIDE_EMPTY, T_scale, T_WIDTH, T_COMPOSIT_STATE, T_STRUCT_START, T_STRUCT_STOP, T_STATE_DESCR, T_AS, T_ID,
  T_FORK, T_JOIN, T_CHOICE, T_CONCURRENT, T_note, T_NOTE_TEXT, T_acc_title, T_acc_title_value, T_acc_descr, T_acc_descr_value, T_acc_descr_multiline_value,
  T_CLICK, T_STRING, T_HREF, T_classDef, T_CLASSDEF_ID, T_DEFAULT_CLASSDEF_ID, T_CLASSDEF_STYLEOPTS, T_DEFAULT, T_style, T_STYLE_IDS, T_STYLEDEF_STYLEOPTS,
  T_class, T_CLASSENTITY_IDS, T_STYLECLASS, T_direction_tb, T_direction_bt, T_direction_rl, T_direction_lr, T_EDGE_STATE, T_STYLE_SEPARATOR, T_left_of,
  T_right_of, T_INVALID
};

const char* const kTokenNames[] = {
    "$end", "INVALID", "NL", "SD", "DESCR", "-->", "HIDE_EMPTY", "scale", "WIDTH", "COMPOSIT_STATE", "STRUCT_START", "STRUCT_STOP", "STATE_DESCR", "AS", "ID",
    "FORK", "JOIN", "CHOICE", "CONCURRENT", "note", "NOTE_TEXT", "acc_title", "acc_title_value", "acc_descr", "acc_descr_value", "acc_descr_multiline_value",
    "CLICK", "STRING", "HREF", "classDef", "CLASSDEF_ID", "DEFAULT_CLASSDEF_ID", "CLASSDEF_STYLEOPTS", "DEFAULT", "style", "STYLE_IDS", "STYLEDEF_STYLEOPTS",
    "class", "CLASSENTITY_IDS", "STYLECLASS", "direction_tb", "direction_bt", "direction_rl", "direction_lr", "EDGE_STATE", "STYLE_SEPARATOR", "left_of",
    "right_of", "INVALID"};

enum lex_state : int {
  S_INITIAL, S_struct, S_STATE, S_STATE_STRING, S_STATE_ID, S_SCALE, S_acc_title, S_acc_descr, S_acc_descr_multiline, S_CLASSDEF, S_CLASSDEFID, S_CLASS,
  S_CLASS_STYLE, S_STYLE, S_STYLEDEF_STYLES, S_NOTE, S_NOTE_ID, S_NOTE_TEXT, S_FLOATING_NOTE, S_FLOATING_NOTE_ID
};

using idx = std::ptrdiff_t;

// Finds the next match of a pattern at or after a position, remembering the answer so that
// asking at each token costs one scan of the text in total.
class finder {
 public:
  explicit finder(std::function<idx(idx)> search) : search_(std::move(search)) {}

  idx operator()(idx q) {
    if (next_ == -2 || q < from_ || (next_ != -1 && q > next_)) {
      next_ = search_(q);
      from_ = q;
    }
    return next_;
  }

 private:
  std::function<idx(idx)> search_;
  idx from_ = 0;
  idx next_ = -2;
};

struct tokens {
  std::vector<int> types;
  std::vector<ustring> texts;
  std::vector<std::size_t> starts;
  // Set when lexing stopped on the grammar's own error rather than on unrecognized text.
  std::optional<std::string> error;
};

tokens tokenize(ustring_view src) {
  const idx n = static_cast<idx>(src.size());
  tokens out;
  std::vector<int> stack;
  int state = S_INITIAL;
  idx p = 0;

  const auto code = [&](idx i) -> int { return i >= 0 && i < n ? src[static_cast<std::size_t>(i)] : -1; };
  const auto ci = [&](int c) { return (c >= 65 && c <= 90) || (c >= 97 && c <= 122); };

  // /direction/gi
  finder next_direction([&](idx q) -> idx {
    for (; q + 9 <= n; q++) {
      bool same = true;
      for (idx k = 0; k < 9 && same; k++) {
        const int c = code(q + k);
        same = ci(c) && (c | 32) == "direction"[k];
      }
      if (same) return q;
    }
    return -1;
  });
  // /<<(?:fork|join|choice)>>|\[\[(?:fork|join|choice)\]\]/gi
  const auto stereo_at = [&](idx q) -> bool {
    const int open = code(q);
    if ((open != '<' && open != '[') || code(q + 1) != open) return false;
    const char close = open == '<' ? '>' : ']';
    for (const char* word : {"fork", "join", "choice"}) {
      const idx len = static_cast<idx>(std::strlen(word));
      bool same = true;
      for (idx k = 0; k < len && same; k++) {
        const int c = code(q + 2 + k);
        same = ci(c) && (c | 32) == word[k];
      }
      if (same && code(q + 2 + len) == close && code(q + 3 + len) == close) return true;
    }
    return false;
  };
  finder next_stereotype([&](idx q) -> idx {
    for (; q < n; q++)
      if (stereo_at(q)) return q;
    return -1;
  });
  finder next_line_end([&](idx q) -> idx {
    for (; q < n; q++)
      if (is_line_end(code(q))) return q;
    return -1;
  });
  finder next_percent([&](idx q) -> idx {
    for (; q + 2 <= n; q++)
      if (code(q) == '%' && code(q + 1) == '%') return q;
    return -1;
  });
  finder next_brace([&](idx q) -> idx {
    for (; q < n; q++)
      if (code(q) == '{') return q;
    return -1;
  });

  // The last `direction <dir>` of each kind on the line being lexed, and where its keyword ends.
  idx dir_from = -1;
  idx dir_to = -1;
  idx dir_at[4] = {-1, -1, -1, -1};
  idx dir_end[4] = {0, 0, 0, 0};
  // The same for <<fork>>, <<join>>, <<choice>> and their [[ ]] spellings.
  idx stereo_from = -1;
  idx stereo_to = -1;
  idx stereo_pos[6] = {-1, -1, -1, -1, -1, -1};

  const auto emit = [&](int type, idx s, idx e) {
    out.types.push_back(type);
    out.texts.push_back(ustring(src.substr(static_cast<std::size_t>(s), static_cast<std::size_t>(e - s))));
    out.starts.push_back(static_cast<std::size_t>(s));
    p = e;
  };
  const auto emit_text = [&](int type, ustring text, idx s, idx e) {
    out.types.push_back(type);
    out.texts.push_back(std::move(text));
    out.starts.push_back(static_cast<std::size_t>(s));
    p = e;
  };
  const auto push = [&](int s) {
    stack.push_back(state);
    state = s;
  };
  const auto pop = [&]() {
    if (!stack.empty()) {
      state = stack.back();
      stack.pop_back();
    }
  };
  // Case-insensitive match of a lowercase ASCII word, as the grammar's /i flag gives.
  const auto word = [&](const char* w, idx q) {
    for (idx i = 0; w[i] != '\0'; i++) {
      const int a = static_cast<unsigned char>(w[i]);
      const int c = code(q + i);
      if (c != a && !(a >= 97 && a <= 122 && c == a - 32)) return false;
    }
    return true;
  };
  const auto ws_end = [&](idx q) {
    idx j = q;
    while (j < n && is_space(code(j))) j++;
    return j;
  };
  const auto word_end = [&](idx q) {
    idx j = q;
    while (j < n && is_word(code(j))) j++;
    return j;
  };
  const auto eol = [&](idx q) -> idx {
    const std::size_t k = src.find(u'\n', static_cast<std::size_t>(q));
    return k == ustring_view::npos ? n : static_cast<idx>(k);
  };
  const auto line_end = [&](idx q) -> idx {
    const idx k = next_line_end(q);
    return k == -1 ? n : k;
  };

  // Rules `.*direction\s+TB[^\n]*` and its three siblings: the first kind, in grammar order,
  // found between q and the end of its line.
  const auto direction = [&](idx q) -> int {
    if (q < dir_from || q > dir_to) {
      const idx first = next_direction(q);
      const idx end = first == -1 ? n : line_end(q);
      dir_from = q;
      dir_to = end;
      dir_at[0] = dir_at[1] = dir_at[2] = dir_at[3] = -1;
      for (idx d = first; d != -1 && d < end; d = next_direction(d + 1)) {
        const idx w = ws_end(d + 9);
        if (w == d + 9) continue;
        const int kind = word("tb", w) ? 0 : word("bt", w) ? 1 : word("rl", w) ? 2 : word("lr", w) ? 3 : -1;
        if (kind == -1) continue;
        dir_at[kind] = d;
        dir_end[kind] = w + 2;
      }
    }
    for (int kind = 0; kind < 4; kind++)
      if (dir_at[kind] >= q) return kind;
    return -1;
  };

  const auto stereotype = [&](idx q) -> int {
    if (q < stereo_from || q > stereo_to) {
      const idx first = next_stereotype(q);
      const idx end = first == -1 ? n : line_end(q);
      stereo_from = q;
      stereo_to = end;
      std::fill(std::begin(stereo_pos), std::end(stereo_pos), -1);
      for (idx d = first; d != -1 && d < end; d = next_stereotype(d + 1)) {
        const int letter = code(d + 2) | 32;
        stereo_pos[(code(d) == 60 ? 0 : 3) + (letter == 102 ? 0 : letter == 106 ? 1 : 2)] = d;
      }
    }
    for (int kind = 0; kind < 6; kind++)
      if (stereo_pos[kind] >= q) return kind;
    return -1;
  };

  // The grammar's processId(): an id ends at an inline `%%`, and a text that starts with one is dropped.
  const auto emit_id = [&](idx s, idx e) -> bool {
    const idx at = next_percent(s);
    if (at == -1 || at + 2 > e) {
      emit(T_ID, s, e);
      return true;
    }
    if (at == s) {
      p = e;
      return false;
    }
    emit(T_ID, s, at);
    return true;
  };

  const auto finish = [&](int type) {
    out.types.push_back(type);
    out.texts.push_back(ustring());
    out.starts.push_back(static_cast<std::size_t>(p));
    return out;
  };

  for (;;) {
    if (p >= n) {
      // Jison tries the current state's rules once more on the empty remainder.
      switch (state) {
        case S_INITIAL: emit(T_NL, n, n); break;
        case S_acc_title: emit(T_acc_title_value, n, n); break;
        case S_acc_descr: emit(T_acc_descr_value, n, n); break;
        case S_acc_descr_multiline: emit(T_acc_descr_multiline_value, n, n); break;
        case S_CLASSDEFID: emit(T_CLASSDEF_STYLEOPTS, n, n); break;
        case S_CLASS_STYLE: emit(T_STYLECLASS, n, n); break;
        case S_STYLEDEF_STYLES: emit(T_STYLEDEF_STYLEOPTS, n, n); break;
        case S_STATE_STRING: emit(T_STATE_DESCR, n, n); break;
        case S_FLOATING_NOTE: emit(T_NOTE_TEXT, n, n); break;
        case S_STATE_ID:
        case S_FLOATING_NOTE_ID: emit(T_ID, n, n); break;
        default: break;
      }
      return finish(T_END);
    }

    const int c = code(p);
    switch (state) {
      case S_INITIAL:
      case S_struct: {
        const bool root = state == S_INITIAL;
        const int lc = c | 32;
        if (root) {
          if (lc == 99 && word("click", p) && !is_word(code(p + 5))) {
            emit(T_CLICK, p, p + 5);
            continue;
          }
          if (lc == 104 && word("href", p) && !is_word(code(p + 4))) {
            emit(T_HREF, p, p + 4);
            continue;
          }
          if (c == 34) {
            const std::size_t close = src.find(u'"', static_cast<std::size_t>(p + 1));
            if (close != ustring_view::npos) {
              emit(T_STRING, p, static_cast<idx>(close) + 1);
              continue;
            }
          }
          if (lc == 100 && word("default", p) && !is_word(code(p + 7))) {
            emit(T_DEFAULT, p, p + 7);
            continue;
          }
          const int kind = direction(p);
          if (kind != -1) {
            emit(T_direction_tb + kind, p, eol(dir_end[kind]));
            continue;
          }
          if (c == 10) {
            idx j = p + 1;
            while (code(j) == 10) j++;
            emit(T_NL, p, j);
            continue;
          }
          if (is_space(c)) {
            p = ws_end(p);
            continue;
          }
        } else if (c != 10 && is_space(c)) {
          idx j = p + 1;
          while (j < n && code(j) != 10 && is_space(code(j))) j++;
          p = j;
          continue;
        }
        if (c == 35 || (c == 37 && code(p + 1) == 37 && code(p + 2) != 123)) {
          p = eol(p);
          continue;
        }
        if (lc == 115) {
          if (root && word("scale", p)) {
            const idx e = ws_end(p + 5);
            if (e > p + 5) {
              emit(T_scale, p, e);
              push(S_SCALE);
              continue;
            }
          }
          if (word("style", p)) {
            const idx e = ws_end(p + 5);
            if (e > p + 5) {
              emit(T_style, p, e);
              push(S_STYLE);
              continue;
            }
          }
          if (word("state", p)) {
            idx e = ws_end(p + 5);
            if (e > p + 5) {
              p = e;
              push(S_STATE);
              continue;
            }
            if (root && word("diagram", p + 5)) {
              const idx from = word("-v2", p + 12) ? p + 15 : p + 12;
              e = ws_end(from);
              if (e > from) {
                emit(T_SD, p, e);
                continue;
              }
            }
          }
        } else if (lc == 99) {
          if (word("class", p)) {
            const bool def = word("def", p + 5);
            const idx from = def ? p + 8 : p + 5;
            const idx e = ws_end(from);
            if (e > from) {
              emit(def ? T_classDef : T_class, p, e);
              push(def ? S_CLASSDEF : S_CLASS);
              continue;
            }
          }
        } else if (lc == 97 && root) {
          if (word("acctitle", p) || word("accdescr", p)) {
            const bool title = (code(p + 3) | 32) == 116;
            const idx q = ws_end(p + 8);
            const int d = code(q);
            if (d == 58) {
              emit(title ? T_acc_title : T_acc_descr, p, ws_end(q + 1));
              push(title ? S_acc_title : S_acc_descr);
              continue;
            }
            if (d == 123 && !title) {
              p = ws_end(q + 1);
              push(S_acc_descr_multiline);
              continue;
            }
          }
        }
        if (root) {
          if (c == 123) {
            emit(T_STRUCT_START, p, p + 1);
            push(S_struct);
            continue;
          }
        } else {
          const int kind = direction(p);
          if (kind != -1) {
            emit(T_direction_tb + kind, p, eol(dir_end[kind]));
            continue;
          }
          if (c == 125) {
            emit(T_STRUCT_STOP, p, p + 1);
            pop();
            continue;
          }
          if (c == 10) {
            p++;
            continue;
          }
        }
        if (lc == 110 && word("note", p)) {
          const idx e = ws_end(p + 4);
          if (e > p + 4) {
            emit(T_note, p, e);
            push(S_NOTE);
            continue;
          }
        }
        if (root && lc == 104 && word("hide empty description", p) && !is_word(code(p + 22))) {
          emit(T_HIDE_EMPTY, p, p + 22);
          continue;
        }
        if (c == 91 && code(p + 1) == 42 && code(p + 2) == 93) {
          emit(T_EDGE_STATE, p, p + 3);
          continue;
        }
        idx j = p;
        while (j < n) {
          const int d = code(j);
          if (d == 58 || d == 45 || d == 123 || is_space(d)) break;
          j++;
        }
        if (j > p) {
          emit_id(p, j);
          continue;
        }
        if (c == 58) {
          j = p + 1;
          while (j < n) {
            const int d = code(j);
            if (d == 10 || d == 59) break;
            if (d == 58) {
              const int after = code(j + 1);
              if (j + 1 >= n || after == 58 || after == 10 || after == 59) break;
              j++;
            }
            j++;
          }
          if (j > p + 1) {
            emit_text(T_DESCR, trim(src.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(j - p))), p, j);
            continue;
          }
          if (code(p + 1) == 58 && code(p + 2) == 58) {
            emit(T_STYLE_SEPARATOR, p, p + 3);
            continue;
          }
        } else if (c == 45 && code(p + 1) == 45) {
          if (code(p + 2) == 62) {
            emit(T_ARROW, p, p + 3);
            continue;
          }
          if (!root) {
            emit(T_CONCURRENT, p, p + 2);
            continue;
          }
        }
        if (!root) return finish(T_ERROR);
        emit(T_INVALID, p, p + 1);
        continue;
      }

      case S_STATE: {
        if (c != 10 && is_space(c)) {
          idx j = p + 1;
          while (j < n && code(j) != 10 && is_space(code(j))) j++;
          p = j;
          continue;
        }
        if (c == 35 || (c == 37 && code(p + 1) == 37 && code(p + 2) != 123)) {
          p = eol(p);
          continue;
        }
        const int kind = stereotype(p);
        if (kind != -1) {
          const idx at = stereo_pos[kind];
          const bool choice = kind == 2 || kind == 5;
          emit_text(choice ? T_CHOICE : kind % 3 == 0 ? T_FORK : T_JOIN, trim(src.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(at - p))), p, at + (choice ? 10 : 8));
          pop();
          continue;
        }
        if (c == 34) {
          p++;
          push(S_STATE_STRING);
          continue;
        }
        const idx q = c == 10 ? ws_end(p) : p;
        if (word("as", q)) {
          const idx e = ws_end(q + 2);
          if (e > q + 2) {
            emit(T_AS, p, e);
            push(S_STATE_ID);
            continue;
          }
        }
        if (c == 10) {
          p++;
          pop();
          continue;
        }
        if (c == 123) {
          emit(T_STRUCT_START, p, p + 1);
          pop();
          push(S_struct);
          continue;
        }
        if (is_word(c)) {
          const idx first = word_end(p);
          const idx second = ws_end(first);
          if (second > first && is_word(code(second))) {
            const idx brace = next_brace(second + 1);
            if (brace != -1 && brace < line_end(second)) {
              out.error = "Error: State name must be a single word. Found: \"" + to_utf8(trim(src.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(brace + 1 - p)))) + "\"";
              return finish(T_ERROR);
            }
          }
        }
        idx j = p + 1;
        while (j < n) {
          const int d = code(j);
          if (d == 123 || is_space(d)) break;
          j++;
        }
        emit(T_COMPOSIT_STATE, p, j);
        continue;
      }

      case S_STATE_STRING: {
        if (c == 34) {
          p++;
          pop();
          continue;
        }
        const std::size_t close = src.find(u'"', static_cast<std::size_t>(p));
        emit(T_STATE_DESCR, p, close == ustring_view::npos ? n : static_cast<idx>(close));
        continue;
      }

      case S_STATE_ID: {
        idx j = p;
        while (j < n) {
          const int d = code(j);
          if (d == 10 || d == 123) break;
          j++;
        }
        if (emit_id(p, j)) pop();
        continue;
      }

      case S_SCALE: {
        if (c >= 48 && c <= 57) {
          idx j = p + 1;
          while (j < n && code(j) >= 48 && code(j) <= 57) j++;
          emit(T_WIDTH, p, j);
          continue;
        }
        const idx q = ws_end(p);
        if (q > p && word("width", q) && !is_word(code(q + 5))) {
          p = q + 5;
          pop();
          continue;
        }
        return finish(T_ERROR);
      }

      case S_acc_title:
        emit(T_acc_title_value, p, eol(p));
        pop();
        continue;

      case S_acc_descr:
        emit(T_acc_descr_value, p, eol(p));
        pop();
        continue;

      case S_acc_descr_multiline: {
        if (c == 125) {
          p++;
          pop();
          continue;
        }
        const std::size_t close = src.find(u'}', static_cast<std::size_t>(p));
        emit(T_acc_descr_multiline_value, p, close == ustring_view::npos ? n : static_cast<idx>(close));
        continue;
      }

      case S_CLASSDEF: {
        const bool is_default = word("default", p) && ws_end(p + 7) > p + 7;
        const idx j = is_default ? p + 7 : word_end(p);
        const idx e = ws_end(j);
        if (j == p || e == j) return finish(T_ERROR);
        emit(is_default ? T_DEFAULT_CLASSDEF_ID : T_CLASSDEF_ID, p, e);
        pop();
        push(S_CLASSDEFID);
        continue;
      }

      case S_CLASSDEFID:
        emit(T_CLASSDEF_STYLEOPTS, p, eol(p));
        pop();
        continue;

      case S_CLASS: {
        idx j = word_end(p);
        if (j == p) return finish(T_ERROR);
        while (code(j) == 44) {
          const idx k = ws_end(j + 1);
          const idx e = word_end(k);
          if (e == k) break;
          j = e;
        }
        emit(T_CLASSENTITY_IDS, p, j);
        pop();
        push(S_CLASS_STYLE);
        continue;
      }

      case S_CLASS_STYLE:
        emit(T_STYLECLASS, p, eol(p));
        pop();
        continue;

      case S_STYLE: {
        idx j = p;
        while (j < n && (is_word(code(j)) || code(j) == 44)) j++;
        const idx e = ws_end(j);
        if (j == p || e == j) return finish(T_ERROR);
        emit(T_STYLE_IDS, p, e);
        pop();
        push(S_STYLEDEF_STYLES);
        continue;
      }

      case S_STYLEDEF_STYLES:
        emit(T_STYLEDEF_STYLEOPTS, p, eol(p));
        pop();
        continue;

      case S_NOTE: {
        if (word("left of", p) && !is_word(code(p + 7))) {
          emit(T_left_of, p, p + 7);
          pop();
          push(S_NOTE_ID);
          continue;
        }
        if (word("right of", p) && !is_word(code(p + 8))) {
          emit(T_right_of, p, p + 8);
          pop();
          push(S_NOTE_ID);
          continue;
        }
        if (c == 34) {
          p++;
          pop();
          push(S_FLOATING_NOTE);
          continue;
        }
        return finish(T_ERROR);
      }

      case S_NOTE_ID: {
        const idx q = ws_end(p);
        idx j = q;
        while (j < n) {
          const int d = code(j);
          if (d == 58 || d == 45 || is_space(d)) break;
          j++;
        }
        if (j == q) return finish(T_ERROR);
        if (emit_id(p, j)) {
          pop();
          push(S_NOTE_TEXT);
        }
        continue;
      }

      case S_NOTE_TEXT: {
        const idx q = ws_end(p);
        if (code(q) == 58) {
          idx j = q + 1;
          while (j < n) {
            const int d = code(j);
            if (d == 58 || d == 10 || d == 59) break;
            j++;
          }
          if (j > q + 1) {
            // Mermaid drops two characters here, expecting " :"; with no space before the colon
            // the first character of the note goes too.
            const idx from = std::min<idx>(p + 2, j);
            emit_text(T_NOTE_TEXT, trim(src.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(j - from))), p, j);
            pop();
            continue;
          }
        }
        idx k = static_cast<idx>(src.find(u'\n', static_cast<std::size_t>(p)));
        if (src.find(u'\n', static_cast<std::size_t>(p)) == ustring_view::npos) k = -1;
        idx e = -1;
        while (k != -1) {
          e = ws_end(k);
          if (word("end note", e) && !is_word(code(e + 8))) break;
          const std::size_t nl = src.find(u'\n', static_cast<std::size_t>(e));
          k = nl == ustring_view::npos ? -1 : static_cast<idx>(nl);
        }
        if (k == -1) return finish(T_ERROR);
        emit_text(T_NOTE_TEXT, trim(src.substr(static_cast<std::size_t>(p), static_cast<std::size_t>(e - p))), p, e + 8);
        pop();
        continue;
      }

      case S_FLOATING_NOTE: {
        const idx q = ws_end(p);
        if (word("as", q)) {
          emit(T_AS, p, ws_end(q + 2));
          pop();
          push(S_FLOATING_NOTE_ID);
          continue;
        }
        if (c == 34) {
          p++;
          continue;
        }
        const std::size_t close = src.find(u'"', static_cast<std::size_t>(p));
        emit(T_NOTE_TEXT, p, close == ustring_view::npos ? n : static_cast<idx>(close));
        continue;
      }

      case S_FLOATING_NOTE_ID:
        if (emit_id(p, eol(p))) pop();
        continue;
    }
  }
}


// ---------------------------------------------------------------------------------------------
// The parser

const char* const kStatement =
    "'NL', 'ID', 'HIDE_EMPTY', 'scale', 'COMPOSIT_STATE', 'STATE_DESCR', 'FORK', 'JOIN', 'CHOICE', 'CONCURRENT', 'note', "
    "'acc_title', 'acc_descr', 'acc_descr_multiline_value', 'CLICK', 'classDef', 'style', 'class', 'direction_tb', "
    "'direction_bt', 'direction_rl', 'direction_lr', 'EDGE_STATE'";

stmt_ptr make_stmt(stmt_kind kind) {
  auto s = std::make_shared<stmt>();
  s->kind = kind;
  return s;
}

stmt_ptr make_text(ustring text) {
  auto s = make_stmt(stmt_kind::text);
  s->text = std::move(text);
  return s;
}

stmt_ptr make_state(ustring id, std::optional<ustring> type = std::nullopt) {
  auto s = make_stmt(stmt_kind::state);
  s->text = std::move(id);
  s->type = std::move(type);
  return s;
}

description describe(ustring text) {
  description d;
  d.text = std::move(text);
  return d;
}

description describe_list(std::vector<ustring> list) {
  description d;
  d.is_array = true;
  d.list = std::move(list);
  return d;
}

// Mermaid's StateDB in its version 2 form, which is what Mermaid uses for both `stateDiagram`
// and `stateDiagram-v2`.
class state_db {
 public:
  explicit state_db(state_model& m) : model_(m) {}

  ustring get_divider_id() { return u"divider-id-" + to_utf16(std::to_string(++model_.divider_count)); }

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

  // The parser's finished document: ids of `[*]` and regions are settled, and then the states,
  // relations and classes of the root document are read.
  void set_root_doc(doc_ptr doc) {
    model_.root_doc = doc;
    translate();
    extract(*doc);
  }

 private:
  // Mermaid's docTranslator: gives each `[*]` an id scoped to its parent, and turns a document
  // split by `--` into one region per part.
  void translate() {
    struct item {
      ustring parent_id;
      stmt_ptr node;
      bool first;
    };
    stmt_ptr root = make_stmt(stmt_kind::root);
    root->text = u"root";
    root->doc = model_.root_doc;
    std::vector<item> pending{{u"root", root, true}};
    while (!pending.empty()) {
      item current = std::move(pending.back());
      pending.pop_back();
      const stmt_ptr& node = current.node;
      if (node->kind == stmt_kind::text) continue;
      if (node->kind == stmt_kind::relation) {
        pending.push_back({current.parent_id, node->state2, false});
        pending.push_back({current.parent_id, node->state1, true});
        continue;
      }
      if (node->kind == stmt_kind::state) {
        if (node->text == u"[*]") {
          node->text = current.parent_id + (current.first ? u"_start" : u"_end");
          node->start = current.first;
        } else {
          node->text = trim(node->text);
        }
      } else if (node->kind != stmt_kind::root) {
        continue;
      }
      if (!node->doc) continue;

      std::vector<stmt_ptr> regions;
      auto now = std::make_shared<std::vector<stmt_ptr>>();
      for (const stmt_ptr& entry : *node->doc) {
        if (entry->kind == stmt_kind::state && entry->type && *entry->type == u"divider" && !entry->doc) {
          auto region = std::make_shared<stmt>(*entry);
          region->doc = now;
          regions.push_back(std::move(region));
          now = std::make_shared<std::vector<stmt_ptr>>();
        } else {
          now->push_back(entry);
        }
      }
      // Mermaid names the region after the last divider with a random id; here it continues the
      // divider numbering. Mermaid also leaves a document that ends with a divider ungrouped and
      // then fails to draw it; here the regions before that divider are kept.
      if (!regions.empty()) {
        if (!now->empty()) {
          auto region = make_state(get_divider_id(), u"divider");
          region->doc = now;
          regions.push_back(std::move(region));
        }
        node->doc = std::make_shared<std::vector<stmt_ptr>>(std::move(regions));
      }
      for (std::size_t k = node->doc->size(); k-- > 0;) pending.push_back({node->text, (*node->doc)[k], true});
    }
  }

  // Only the root document's statements become states, relations and classes here; the
  // statements inside composites are read when the graph is built.
  void extract(const std::vector<stmt_ptr>& doc) {
    model_.states.clear();
    model_.classes.clear();
    for (const stmt_ptr& item : doc) {
      switch (item->kind) {
        case stmt_kind::state:
          add_state(trim(item->text), item->type, item->doc, item->descr, item->note);
          break;
        case stmt_kind::relation:
          add_relation(*item->state1, *item->state2);
          break;
        case stmt_kind::class_def:
          add_style_class(trim(item->text), item->value);
          break;
        case stmt_kind::style: {
          std::vector<ustring> styles;
          for (const ustring& part : split(item->value, u',')) {
            ustring cleaned;
            for (const char16_t c : part)
              if (c != u';') cleaned += c;
            styles.push_back(trim(cleaned));
          }
          for (const stmt_ptr& state : named(trim(item->text))) state->styles = styles;
          break;
        }
        case stmt_kind::apply_class:
          set_css_class(trim(item->text), item->value);
          break;
        case stmt_kind::click:
          model_.links[item->text] = {item->url, item->tooltip};
          break;
        default:
          break;
      }
    }
    build_graph(doc);
  }

  static std::vector<ustring> split(const ustring& text, char16_t sep) {
    std::vector<ustring> out;
    std::size_t from = 0;
    for (;;) {
      const std::size_t at = text.find(sep, from);
      if (at == ustring::npos) {
        out.push_back(text.substr(from));
        return out;
      }
      out.push_back(text.substr(from, at - from));
      from = at + 1;
    }
  }

  void add_state(const ustring& id, const std::optional<ustring>& type, const doc_ptr& doc, const std::optional<description>& descr, const std::optional<state_note>& note,
                 const std::vector<ustring>* classes = nullptr) {
    const ustring trimmed = trim(id);
    auto it = model_.states.find(trimmed);
    stmt_ptr state;
    if (it == model_.states.end()) {
      state = make_state(trimmed, type ? type : std::optional<ustring>(u"default"));
      state->doc = doc;
      state->note = note;
      model_.states[trimmed] = state;
    } else {
      state = it->second;
      if (!state->doc) state->doc = doc;
      if (!state->type) state->type = type ? type : std::optional<ustring>(u"default");
    }
    if (descr && descr->truthy()) {
      if (descr->is_array)
        for (const ustring& text : descr->list) add_description(trimmed, trim(text));
      else
        add_description(trimmed, trim(descr->text));
    }
    if (note) state->note = note;
    if (classes != nullptr)
      for (const ustring& name : *classes) set_css_class(trimmed, trim(name));
  }

  void add_relation(const stmt& item1, const stmt& item2) {
    for (const stmt* item : {&item1, &item2}) add_state(item->text, item->type, item->doc, item->descr, item->note, &item->classes);
  }

  void add_description(const ustring& id, const ustring& descr) {
    const auto it = model_.states.find(id);
    if (it == model_.states.end()) return;
    if (!descr.empty() && descr[0] == u':') {
      ustring copy = descr;
      copy.erase(0, 1);
      it->second->descriptions.push_back(trim(copy));
    } else {
      it->second->descriptions.push_back(descr);
    }
  }

  void add_style_class(const ustring& id, const ustring& style_attributes) {
    auto it = model_.classes.find(id);
    if (it == model_.classes.end()) it = model_.classes.emplace(id, state_class_def{id, {}, {}}).first;
    state_class_def& found = it->second;
    if (style_attributes.empty()) return;
    for (const ustring& attrib : split(style_attributes, u',')) {
      ustring fixed = attrib;
      const std::size_t semi = fixed.find(u';');
      if (semi != ustring::npos) fixed.erase(semi, 1);
      fixed = trim(fixed);
      if (attrib.find(u"color") != ustring::npos) {
        ustring text_style = fixed;
        const std::size_t fill = text_style.find(u"fill");
        if (fill != ustring::npos) text_style.replace(fill, 4, u"bgFill");
        const std::size_t color = text_style.find(u"color");
        if (color != ustring::npos) text_style.replace(color, 5, u"fill");
        found.text_styles.push_back(std::move(text_style));
      }
      found.styles.push_back(std::move(fixed));
    }
  }

  void set_css_class(const ustring& ids, const ustring& name) {
    for (const stmt_ptr& state : named(ids)) state->classes.push_back(name);
  }

  // The states a comma-separated list of ids names, created where they do not exist yet. As in
  // Mermaid, an id is looked up as written and trimmed only when it has to be created.
  std::vector<stmt_ptr> named(const ustring& ids) {
    std::vector<stmt_ptr> out;
    for (const ustring& id : split(ids, u',')) {
      auto it = model_.states.find(id);
      if (it == model_.states.end()) {
        add_state(id, std::nullopt, nullptr, std::nullopt, std::nullopt);
        it = model_.states.find(trim(id));
      }
      if (it != model_.states.end()) out.push_back(it->second);
    }
    return out;
  }

  // Mermaid's dataFetcher: walks the documents and resolves them into the nodes and edges that are drawn.
  void build_graph(const std::vector<stmt_ptr>& root_doc);

  state_model& model_;
};

// The direction a document sets: its last `direction` statement, or top to bottom.
ustring doc_direction(const std::vector<stmt_ptr>& doc) {
  ustring dir = u"TB";
  for (const stmt_ptr& item : doc)
    if (item->kind == stmt_kind::dir) dir = item->text;
  return dir;
}

struct draft {
  std::optional<ustring> shape;
  description descr;
  ustring css_classes;
  std::vector<ustring> css_styles;
  std::optional<ustring> type;
  std::optional<ustring> dir;
};

struct frame {
  stmt_ptr item;
  doc_ptr doc;
  std::size_t at = 0;
  bool alt = false;
};

std::size_t description_length(const description& d) { return d.is_array ? d.list.size() : d.text.size(); }

// Splits the text at spaces, keeping empty pieces.
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

void state_db::build_graph(const std::vector<stmt_ptr>& root_doc) {
  state_graph graph;
  std::map<ustring, std::size_t> by_id;
  std::map<ustring, draft> drafts;
  int counter = 0;

  const auto insert = [&](state_node node) {
    // The two literal ids are what DOMPurify leaves of `<<fork>>` and `<<choice>>` in Mermaid; kept for parity.
    if (node.id.empty() || node.id == u"</join></fork>" || node.id == u"</choice>") return;
    if (!node.css_classes.empty()) {
      // One copy, however many classes: a state named in a class statement many times has a long list.
      for (const ustring& name : split_spaces(node.css_classes)) {
        const auto def = model_.classes.find(name);
        if (def == model_.classes.end()) continue;
        for (const ustring& style : def->second.styles) node.css_compiled_styles.push_back(style);
      }
    }
    const auto existing = by_id.find(node.id);
    if (existing != by_id.end()) {
      state_node& old = graph.nodes[existing->second];
      const std::optional<ustring> parent_id = node.parent_id ? node.parent_id : old.parent_id;
      // Object.assign: what the new node sets replaces what the old one had. Properties the new
      // node does not have at all are kept.
      const std::optional<ustring> position = node.position ? node.position : old.position;
      const std::optional<ustring> note_for = node.note_for ? node.note_for : old.note_for;
      old = std::move(node);
      old.parent_id = parent_id;
      old.position = position;
      old.note_for = note_for;
    } else {
      by_id[node.id] = graph.nodes.size();
      graph.nodes.push_back(std::move(node));
    }
  };

  const auto visit = [&](const stmt_ptr& parent, const stmt& item, bool alt) {
    const ustring& id = item.text;
    if (id == u"root") return;
    const auto known_it = model_.states.find(id);
    const stmt* known = known_it == model_.states.end() ? nullptr : known_it->second.get();
    ustring class_str;
    if (known != nullptr)
      for (std::size_t k = 0; k < known->classes.size(); k++) class_str += (k > 0 ? u" " : u"") + known->classes[k];
    const std::vector<ustring> no_styles;
    const std::vector<ustring>& styles = known != nullptr ? known->styles : no_styles;

    auto found = drafts.find(id);
    if (found == drafts.end()) {
      draft d;
      d.shape = u"rect";
      if (item.start && *item.start) d.shape = u"stateStart";
      else if (item.start && !*item.start) d.shape = u"stateEnd";
      // `item.type !== 'default'`: an absent type differs from it too, and gives no shape.
      if (!item.type || *item.type != u"default") d.shape = item.type;
      d.descr = describe(id);
      d.css_classes = class_str + u" statediagram-state";
      d.css_styles = styles;
      found = drafts.emplace(id, std::move(d)).first;
    }
    draft& dr = found->second;

    if (item.descr && item.descr->truthy()) {
      const description& incoming = *item.descr;
      if (dr.descr.is_array) {
        dr.shape = u"rectWithTitle";
        if (incoming.is_array) dr.descr.list.insert(dr.descr.list.end(), incoming.list.begin(), incoming.list.end());
        else dr.descr.list.push_back(incoming.text);
      } else if (!dr.descr.text.empty()) {
        dr.shape = u"rectWithTitle";
        std::vector<ustring> list;
        if (dr.descr.text != id) list.push_back(dr.descr.text);
        if (incoming.is_array) list.insert(list.end(), incoming.list.begin(), incoming.list.end());
        else list.push_back(incoming.text);
        dr.descr = describe_list(std::move(list));
      } else {
        dr.shape = u"rect";
        dr.descr = incoming;
      }
    }
    if (description_length(dr.descr) == 1 && dr.shape && *dr.shape == u"rectWithTitle") {
      dr.shape = dr.type && *dr.type == u"group" ? u"roundedWithTitle" : u"rect";
    }

    if ((!dr.type || dr.type->empty()) && item.doc) {
      dr.type = u"group";
      dr.dir = doc_direction(*item.doc);
      dr.shape = item.type && *item.type == u"divider" ? u"divider" : u"roundedWithTitle";
      dr.css_classes = dr.css_classes + u" statediagram-cluster " + (alt ? u"statediagram-cluster-alt" : u"");
    }

    const description& full = dr.descr;
    state_node node;
    node.id = id;
    node.shape = dr.shape;
    node.label = full.is_array ? (full.list.empty() ? ustring() : full.list[0]) : full.text;
    if (full.is_array) node.description.assign(full.list.begin() + (full.list.empty() ? 0 : 1), full.list.end());
    node.css_classes = dr.css_classes;
    node.css_styles = dr.css_styles;
    node.type = dr.type;
    node.is_group = dr.type && *dr.type == u"group";
    node.dir = dr.dir;
    if (node.shape && *node.shape == u"divider") node.label = u"";
    if (parent && parent->text != u"root") node.parent_id = parent->text;

    if (!item.note) {
      insert(std::move(node));
      return;
    }
    // Mermaid puts each note in a group of its own, named after the state. Pele keeps that
    // group in the model and lays the note out beside its state instead.
    const state_note& note = *item.note;
    const auto aside = [&](const ustring& aside_id, const ustring& shape, const ustring& css_classes, bool is_group) {
      state_node a;
      a.id = aside_id;
      a.shape = shape;
      a.label = note.text;
      a.css_classes = css_classes;
      a.type = is_group ? u"group" : u"node";
      a.is_group = is_group;
      a.position = note.position;
      return a;
    };
    state_node note_node = aside(id + u"----note-" + to_utf16(std::to_string(counter++)), u"note", u"statediagram-note", false);
    note_node.parent_id = id + u"----parent";
    note_node.note_for = id;
    insert(aside(*note_node.parent_id, u"noteGroup", dr.css_classes, true));
    const ustring note_id = note_node.id;
    insert(std::move(note_node));
    insert(std::move(node));
    const bool left = note.position && *note.position == u"left of";
    const ustring from = left ? note_id : id;
    const ustring to = left ? id : note_id;
    state_edge edge;
    edge.id = from + u"-" + to;
    edge.start = from;
    edge.end = to;
    edge.classes = u"transition note-edge";
    edge.dashed = true;
    graph.edges.push_back(std::move(edge));
  };

  std::vector<frame> stack;
  stack.push_back({nullptr, std::make_shared<std::vector<stmt_ptr>>(root_doc), 0, true});
  while (!stack.empty()) {
    const std::size_t top = stack.size() - 1;
    if (stack[top].at >= stack[top].doc->size()) {
      stack.pop_back();
      continue;
    }
    const stmt_ptr item = (*stack[top].doc)[stack[top].at++];
    const stmt_ptr parent = stack[top].item;
    const bool alt = stack[top].alt;
    if (item->kind == stmt_kind::state) {
      visit(parent, *item, alt);
      if (item->doc) stack.push_back({item, item->doc, 0, !alt});
    } else if (item->kind == stmt_kind::relation) {
      visit(parent, *item->state1, alt);
      visit(parent, *item->state2, alt);
      state_edge edge;
      edge.id = u"edge" + to_utf16(std::to_string(counter++));
      edge.start = item->state1->text;
      edge.end = item->state2->text;
      edge.label = item->relation_text ? *item->relation_text : ustring();
      edge.classes = u"transition";
      graph.edges.push_back(std::move(edge));
    }
  }

  for (const state_node& node : graph.nodes) {
    if (node.is_group && !node.description.empty()) {
      error_info info;
      info.type = "state";
      throw pele_error("Group nodes can only have label. Remove the additional description for node [" + to_utf8(node.id) + "]", error_code::semantic, std::move(info));
    }
  }
  model_.graph = std::move(graph);
}

void parse(const ustring& src, state_model& model) {
  const tokens tk = tokenize(src);
  const std::vector<int>& types = tk.types;
  const std::vector<ustring>& texts = tk.texts;
  std::size_t i = 0;
  state_db db(model);

  const auto type_at = [&](std::size_t k) { return k < types.size() ? types[k] : -1; };
  const auto fail = [&](const std::string& expected) {
    const std::size_t at = std::min(i, tk.starts.size() - 1);
    const int t = type_at(i);
    if (t == T_ERROR) {
      if (tk.error) {
        const source_location loc = locate(src, tk.starts[at]);
        error_info info;
        info.type = "state";
        info.line = loc.line;
        info.column = loc.column;
        info.snippet = to_utf8(loc.snippet);
        throw pele_error(*tk.error, error_code::syntax, std::move(info));
      }
      throw syntax_error("state", src, tk.starts[at], u"", true);
    }
    throw syntax_error("state", src, tk.starts[at], to_utf16(std::string("Expecting ") + expected + ", got '" + kTokenNames[t] + "'"));
  };
  const auto expect = [&](int type) -> ustring {
    if (type_at(i) != type) fail(std::string("'") + kTokenNames[type] + "'");
    return texts[i++];
  };
  const auto id_statement = [&]() -> stmt_ptr {
    const int t = type_at(i);
    if (t != T_ID && t != T_EDGE_STATE) fail("'ID', 'EDGE_STATE'");
    ustring id = trim(texts[i++]);
    auto s = make_state(std::move(id), u"default");
    s->descr = describe(u"");
    if (type_at(i) != T_STYLE_SEPARATOR) return s;
    i++;
    s->classes.push_back(trim(expect(T_ID)));
    return s;
  };

  while (type_at(i) == T_NL) i++;
  if (type_at(i) != T_SD) fail("'NL', 'SD'");
  i++;

  doc_ptr doc = std::make_shared<std::vector<stmt_ptr>>();
  std::vector<doc_ptr> outer;
  // The grammar drops a statement whose value is the string it uses for a blank line.
  const auto add = [&](stmt_ptr value) {
    if (value->kind == stmt_kind::text && value->text == u"nl") return;
    doc->push_back(std::move(value));
  };
  const auto open = [&](ustring id, ustring description_text) {
    i++;
    auto inner = std::make_shared<std::vector<stmt_ptr>>();
    auto s = make_state(std::move(id), u"default");
    s->descr = describe(std::move(description_text));
    s->doc = inner;
    doc->push_back(std::move(s));
    outer.push_back(doc);
    doc = inner;
  };
  static const char16_t* const directions[] = {u"TB", u"BT", u"RL", u"LR"};

  for (;;) {
    const int t = type_at(i);
    switch (t) {
      case T_NL:
        i++;
        break;
      case T_ID:
      case T_EDGE_STATE: {
        stmt_ptr first = id_statement();
        if (type_at(i) == T_DESCR) {
          ustring text = texts[i++];
          // trimColon
          first->descr = describe(!text.empty() && text[0] == u':' ? trim(text.substr(1)) : trim(text));
          add(first);
        } else if (type_at(i) == T_ARROW) {
          i++;
          stmt_ptr second = id_statement();
          auto relation = make_stmt(stmt_kind::relation);
          relation->state1 = first;
          relation->state2 = second;
          if (type_at(i) == T_DESCR) {
            const ustring text = texts[i++];
            relation->relation_text = !text.empty() && text[0] == u':' ? trim(text.substr(1)) : trim(text);
          }
          add(relation);
        } else {
          add(first);
        }
        break;
      }
      case T_HIDE_EMPTY:
        add(make_text(texts[i++]));
        break;
      case T_scale:
        i++;
        expect(T_WIDTH);
        add(make_text(texts[i - 2]));
        break;
      case T_COMPOSIT_STATE:
        if (type_at(i + 1) == T_STRUCT_START) {
          ustring id = texts[i++];
          open(std::move(id), u"");
        } else {
          add(make_text(texts[i++]));
        }
        break;
      case T_STATE_DESCR: {
        const ustring text = texts[i++];
        expect(T_AS);
        const ustring id = expect(T_ID);
        if (type_at(i) == T_STRUCT_START) {
          open(id, text);
        } else if (id.find(u':') != ustring::npos) {
          const std::size_t colon = id.find(u':');
          const std::size_t second = id.find(u':', colon + 1);
          auto s = make_state(id.substr(0, colon), u"default");
          s->descr = describe_list({trim(text), id.substr(colon + 1, second == ustring::npos ? ustring::npos : second - colon - 1)});
          add(s);
        } else {
          auto s = make_state(id, u"default");
          s->descr = describe(trim(text));
          add(s);
        }
        break;
      }
      case T_FORK:
      case T_JOIN:
      case T_CHOICE: {
        auto s = make_state(texts[i++], t == T_FORK ? u"fork" : t == T_JOIN ? u"join" : u"choice");
        add(s);
        break;
      }
      case T_CONCURRENT:
        i++;
        add(make_state(db.get_divider_id(), u"divider"));
        break;
      case T_note: {
        const ustring keyword = texts[i++];
        if (type_at(i) == T_left_of || type_at(i) == T_right_of) {
          ustring position = trim(texts[i++]);
          ustring id = trim(expect(T_ID));
          auto s = make_state(std::move(id));
          s->note = state_note{std::move(position), trim(expect(T_NOTE_TEXT))};
          add(s);
        } else {
          if (type_at(i) != T_NOTE_TEXT) fail("'NOTE_TEXT', 'left_of', 'right_of'");
          i++;
          expect(T_AS);
          expect(T_ID);
          add(make_text(keyword));
        }
        break;
      }
      case T_direction_tb:
      case T_direction_bt:
      case T_direction_rl:
      case T_direction_lr: {
        const ustring value = directions[t - T_direction_tb];
        i++;
        auto s = make_stmt(stmt_kind::dir);
        s->text = value;
        add(s);
        break;
      }
      case T_acc_title: {
        i++;
        const ustring value = trim(expect(T_acc_title_value));
        db.set_acc_title(value);
        add(make_text(value));
        break;
      }
      case T_acc_descr: {
        i++;
        const ustring value = trim(expect(T_acc_descr_value));
        db.set_acc_description(value);
        add(make_text(value));
        break;
      }
      case T_acc_descr_multiline_value: {
        const ustring value = trim(texts[i++]);
        db.set_acc_description(value);
        add(make_text(value));
        break;
      }
      case T_CLICK: {
        i++;
        stmt_ptr id = id_statement();
        ustring url;
        ustring tooltip;
        if (type_at(i) == T_HREF) {
          i++;
          url = expect(T_STRING);
        } else {
          if (type_at(i) != T_STRING) fail("'STRING', 'HREF'");
          url = texts[i++];
          tooltip = expect(T_STRING);
        }
        expect(T_NL);
        auto s = make_stmt(stmt_kind::click);
        s->text = id->text;
        s->url = std::move(url);
        s->tooltip = std::move(tooltip);
        add(s);
        break;
      }
      case T_classDef: {
        i++;
        ustring id = trim(expect(T_CLASSDEF_ID));
        auto s = make_stmt(stmt_kind::class_def);
        s->text = std::move(id);
        s->value = trim(expect(T_CLASSDEF_STYLEOPTS));
        add(s);
        break;
      }
      case T_style: {
        i++;
        ustring id = trim(expect(T_STYLE_IDS));
        auto s = make_stmt(stmt_kind::style);
        s->text = std::move(id);
        s->value = trim(expect(T_STYLEDEF_STYLEOPTS));
        add(s);
        break;
      }
      case T_class: {
        i++;
        ustring id = trim(expect(T_CLASSENTITY_IDS));
        auto s = make_stmt(stmt_kind::apply_class);
        s->text = std::move(id);
        s->value = trim(expect(T_STYLECLASS));
        add(s);
        break;
      }
      case T_STRUCT_STOP:
        if (outer.empty()) fail(std::string("'$end', ") + kStatement);
        i++;
        doc = outer.back();
        outer.pop_back();
        break;
      case T_END:
        if (!outer.empty()) fail(std::string("'STRUCT_STOP', ") + kStatement);
        db.set_root_doc(doc);
        return;
      default:
        fail(!outer.empty() ? std::string("'STRUCT_STOP', ") + kStatement : std::string("'$end', ") + kStatement);
    }
  }
}

}  // namespace

ustring state_model::direction() const {
  ustring dir = u"TB";
  for (const stmt_ptr& item : *root_doc)
    if (item->kind == stmt_kind::dir) dir = item->text;
  return dir;
}

void parse_state(const ustring& source, state_model& model) { parse(source, model); }

namespace {

// ---------------------------------------------------------------------------------------------
// Renderer

using svg::esc;
using svg::esc_text;
using svg::num;

constexpr double kNodeSep = 40;
constexpr double kEdgeSep = 16;
constexpr double kRankSep = 44;
constexpr double kGroupPad = 16;
constexpr double kRegionPad = 10;
constexpr double kPadX = 16;
constexpr double kPadY = 10;
constexpr double kLoop = 26;
constexpr double kFold = 8;
constexpr double kNoteWrap = 320;
constexpr double kCorner = 10;
constexpr double kBar = 8;
const char* const kSurface = " fill=\"var(--_s)\" stroke=\"var(--_b)\"";
const char* const kRule = " fill=\"none\" stroke=\"var(--_b)\"";

enum kind : int { K_State, K_Described, K_Start, K_End, K_Choice, K_Fork, K_Join, K_Note, K_Composite, K_Region, K_Hidden };

const char* const kClasses[] = {"pele-node pele-state", "pele-node pele-state", "pele-node pele-state-start", "pele-node pele-state-end", "pele-node pele-state-choice",
                                "pele-node pele-state-fork", "pele-node pele-state-join", "pele-note", "pele-cluster pele-state-composite"};

kind kind_of_shape(const std::optional<ustring>& shape) {
  if (!shape) return K_State;
  if (*shape == u"rectWithTitle") return K_Described;
  if (*shape == u"stateStart") return K_Start;
  if (*shape == u"stateEnd") return K_End;
  if (*shape == u"choice") return K_Choice;
  if (*shape == u"fork") return K_Fork;
  if (*shape == u"join") return K_Join;
  if (*shape == u"note") return K_Note;
  if (*shape == u"divider") return K_Hidden;
  return K_State;
}

struct view {
  // Absent for the unseen helpers the layout needs.
  const state_node* node = nullptr;
  kind k = K_State;
  std::optional<theme::resolved_style> style;
  label text;
  std::vector<label> lines;
  double w = 0;
  double h = 0;
  double title_h = 0;
};

bool vertical(flow_dir dir) { return dir == flow_dir::tb || dir == flow_dir::bt; }

// A positive number set in the config, up to 2000, or the fallback.
double number_option(const yaml_value* section, const char* key, double fallback) {
  if (section == nullptr || !section->is_object()) return fallback;
  const yaml_value* value = section->find(key);
  return value != nullptr && value->is_number() && value->as_number() > 0 ? std::min(value->as_number(), 2000.0) : fallback;
}

// Mermaid keeps the quotes of a click statement's strings and strips them when it draws.
ustring unquote(const ustring& text) {
  std::size_t from = 0;
  std::size_t to = text.size();
  while (from < to && text[from] == u'"') from++;
  while (to > from && text[to - 1] == u'"') to--;
  return text.substr(from, to - from);
}

double clamp(double value, double min, double max) { return value < min ? min : value > max ? max : value; }

std::string rect(double x, double y, double w, double h, const std::string& rest) {
  return "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\"" + rest + "/>";
}

compound_node group(flow_dir dir, double pad_x, double pad_top, double pad_bottom) {
  compound_node c(0, 0);
  c.is_group = true;
  c.has_dir = true;
  c.dir = dir;
  c.pad_x = pad_x;
  c.pad_top = pad_top;
  c.pad_bottom = pad_bottom;
  return c;
}

// The classes Mermaid itself puts on states, which Pele names in its own way:
// /(?:^| )statediagram-(?:state|cluster-alt|cluster|note)(?= |$)/g removed.
ustring strip_mermaid_classes(const ustring& classes) {
  static const std::u16string prefix = u"statediagram-";
  static const std::u16string names[] = {u"state", u"cluster-alt", u"cluster", u"note"};
  ustring out;
  std::size_t i = 0;
  while (i < classes.size()) {
    std::size_t at = ustring::npos;
    if (i == 0 && classes.compare(0, prefix.size(), prefix) == 0) at = 0;
    else if (classes[i] == u' ' && classes.compare(i + 1, prefix.size(), prefix) == 0) at = i + 1;
    if (at != ustring::npos) {
      bool matched = false;
      for (const std::u16string& name : names) {
        const std::size_t end = at + prefix.size() + name.size();
        if (classes.compare(at + prefix.size(), name.size(), name) == 0 && (end == classes.size() || classes[end] == u' ')) {
          i = end;
          matched = true;
          break;
        }
      }
      if (matched) continue;
    }
    out += classes[i++];
  }
  return out;
}

// Picks where a composite's title sits in its title bar so that edges entering from above do not cross it.
double title_x(const compound_node& c, double width, const std::map<double, std::vector<double>>& crossings) {
  const auto found = crossings.find(js_round(c.y - c.h / 2));
  if (found == crossings.end() || width == 0) return c.x;
  const double left = c.x - c.w / 2;
  const double half = width / 2;
  double best = c.x;
  double best_gap = -1;
  for (const double x : {c.x, left + kGroupPad + half, left + c.w - kGroupPad - half}) {
    double gap = std::numeric_limits<double>::infinity();
    for (const double cx : found->second)
      if (cx > left && cx < left + c.w) gap = std::min(gap, std::abs(cx - x) - half);
    if (gap >= 6) return x;
    if (gap > best_gap + 0.5) {
      best_gap = gap;
      best = x;
    }
  }
  return best;
}

class state_renderer {
 public:
  state_renderer(const state_model& db, const config& settings, const render_options& options) : db_(db), settings_(settings), options_(options), icons_(options.icons) {}

  rendered render() {
    const std::string direction = to_utf8(db_.direction());
    if (!fit_width::runs_across(direction)) return draw(false);
    return fit_width::turn_to_fit(options_, [&](bool down) { return draw(down); });
  }

 private:
  // `turned` draws a diagram that runs across as one that runs down.
  rendered draw(bool turned) {
    const state_graph& graph = db_.graph;
    // State diagrams follow the flowchart curve.
    std::string curve;
    bool has_curve = false;
    if (const yaml_value* flow = settings_.find("flowchart")) {
      const yaml_value* c = flow->is_object() ? flow->find("curve") : nullptr;
      if (c != nullptr && c->is_string()) {
        curve = to_utf8(c->as_string());
        has_curve = true;
      }
    }
    const double size = options_.font_size ? *options_.font_size : 16;
    const double small = js_round(size * 0.875);
    const metrics_measurer default_measurer;
    const text_measurer& measurer = options_.measurer != nullptr ? *options_.measurer : default_measurer;
    const label_layouter layouter(measurer, &icons_);
    const yaml_value* conf = settings_.find("state");
    const double wrap_width = number_option(conf, "wrappingWidth", 200);
    const double pad = options_.padding ? *options_.padding : 8;
    const flow_dir root_dir = turned ? flow_dir::tb : parse_direction(to_utf8(db_.direction()));
    const label no_label = layouter.layout(ustring_view(), false, size, 0);
    const auto text = [&](const ustring& raw, double font_size, double wrap, int style = 0) { return layouter.layout(raw, true, font_size, wrap, style); };
    constexpr double infinity = std::numeric_limits<double>::infinity();

    std::map<ustring, std::size_t> index;
    std::vector<view> views;
    std::vector<compound_node> cnodes;
    const auto add = [&](const state_node* node, kind k, const compound_node* c) -> std::size_t {
      view v;
      v.node = node;
      v.k = k;
      v.text = no_label;
      views.push_back(std::move(v));
      if (c != nullptr) cnodes.push_back(*c);
      return views.size() - 1;
    };
    for (const state_node& node : graph.nodes) {
      if (node.shape && *node.shape == u"noteGroup") continue;
      const kind k = node.is_group ? (node.shape && *node.shape == u"divider" ? K_Region : K_Composite) : kind_of_shape(node.shape);
      index[node.id] = add(&node, k, nullptr);
    }
    const std::size_t states = views.size();

    std::vector<int> logical;
    for (std::size_t i = 0; i < states; i++) {
      const auto& parent_id = views[i].node->parent_id;
      const auto found = parent_id ? index.find(*parent_id) : index.end();
      const bool ok = found != index.end() && found->second != i && views[found->second].node->is_group;
      logical.push_back(ok ? static_cast<int>(found->second) : -1);
    }
    // Composites that are redeclared inside each other form a ring with no way in from the root.
    // One member of each ring moves to the root, which leaves a tree.
    {
      std::vector<std::vector<int>> kids(states);
      std::vector<int> queue;
      std::vector<char> placed(states, 0);
      for (std::size_t i = 0; i < states; i++) {
        if (logical[i] == -1) {
          placed[i] = 1;
          queue.push_back(static_cast<int>(i));
        } else {
          kids[static_cast<std::size_t>(logical[i])].push_back(static_cast<int>(i));
        }
      }
      std::size_t done = 0;
      const auto settle = [&]() {
        for (; done < queue.size(); done++) {
          for (const int kid : kids[static_cast<std::size_t>(queue[done])]) {
            if (placed[static_cast<std::size_t>(kid)]) continue;
            placed[static_cast<std::size_t>(kid)] = 1;
            queue.push_back(kid);
          }
        }
      };
      settle();
      std::vector<int> seen(states, -1);
      for (std::size_t i = 0; i < states; i++) {
        if (placed[i]) continue;
        int ring = static_cast<int>(i);
        while (seen[static_cast<std::size_t>(ring)] != static_cast<int>(i)) {
          seen[static_cast<std::size_t>(ring)] = static_cast<int>(i);
          ring = logical[static_cast<std::size_t>(ring)];
        }
        logical[static_cast<std::size_t>(ring)] = -1;
        placed[static_cast<std::size_t>(ring)] = 1;
        queue.push_back(ring);
        settle();
      }
    }
    std::vector<int> owners(states, -1);
    for (std::size_t i = 0; i < states; i++) {
      const auto& note_for = views[i].node->note_for;
      const auto found = note_for ? index.find(*note_for) : index.end();
      if (found == index.end()) continue;
      owners[i] = static_cast<int>(found->second);
      logical[i] = logical[found->second];
    }

    // As in Mermaid, a composite that sets no direction runs top to bottom whatever surrounds it.
    const auto inner_dir = [&](int composite) { return parse_direction(views[static_cast<std::size_t>(composite)].node->dir ? to_utf8(*views[static_cast<std::size_t>(composite)].node->dir) : "TB"); };
    // Whether the flow around a state runs top to bottom or bottom to top.
    const auto across = [&](std::size_t i) { return vertical(logical[i] == -1 ? root_dir : inner_dir(logical[i])); };

    std::vector<int> degree(states, 0);
    std::vector<char> noted(states, 0);
    // When each state first takes part in a transition. Layout follows this order rather than
    // the order of declaration, so a state declared up front does not become the top of the flow.
    std::vector<double> appear(states, infinity);
    int appeared = 0;
    std::vector<int> counts(2 * states, 0);
    for (const state_edge& edge : graph.edges) {
      const auto src = index.find(edge.start);
      const auto dst = index.find(edge.end);
      if (src == index.end() || dst == index.end()) continue;
      if (edge.dashed) {
        noted[views[src->second].k == K_Note ? dst->second : src->second] = 1;
        continue;
      }
      degree[src->second] = std::max(degree[src->second], ++counts[2 * src->second]);
      degree[dst->second] = std::max(degree[dst->second], ++counts[2 * dst->second + 1]);
      if (appear[src->second] == infinity) appear[src->second] = appeared++;
      if (appear[dst->second] == infinity) appear[dst->second] = appeared++;
    }

    for (std::size_t i = 0; i < states; i++) {
      view& v = views[i];
      const state_node& node = *v.node;
      std::vector<ustring> declarations = node.css_compiled_styles;
      if (!node.css_styles.empty()) declarations.insert(declarations.end(), node.css_styles.begin(), node.css_styles.end());
      v.style = theme::resolve_style(declarations);
      const theme::resolved_style& style = *v.style;
      const int text_style_bits = (style.bold ? text_style::bold : 0) | (style.italic ? text_style::italic : 0);
      const double font_size = style.font_size ? *style.font_size : size;
      std::optional<compound_node> c;
      switch (v.k) {
        case K_Composite:
          v.text = text(node.label, font_size, 4000, text_style_bits);
          v.title_h = v.text.height > 0 ? v.text.height + 12 : 0;
          c = group(inner_dir(static_cast<int>(i)), kGroupPad, v.title_h + kGroupPad, kGroupPad);
          c->min_w = v.text.width + 2 * kGroupPad;
          break;
        case K_Region:
          c = group(inner_dir(static_cast<int>(i)), kRegionPad, kRegionPad, kRegionPad);
          break;
        case K_Start:
          v.w = v.h = 14;
          break;
        case K_End:
          v.w = v.h = 18;
          break;
        case K_Choice:
          v.w = v.h = 30;
          break;
        case K_Fork:
        case K_Join: {
          const double length = clamp(40 * degree[i] + 16, 72, 320);
          v.w = across(i) ? length : kBar;
          v.h = across(i) ? kBar : length;
          break;
        }
        case K_Hidden:
          break;
        case K_Note:
          v.text = text(node.label, small, std::max(wrap_width, kNoteWrap));
          v.w = v.text.width + 24;
          v.h = v.text.height + 16;
          break;
        default: {
          v.text = text(node.label, font_size, wrap_width, text_style_bits);
          double width = v.text.width;
          double height = v.text.height + 2 * kPadY;
          if (v.k == K_Described) {
            height += 14;
            for (const ustring& line : node.description) {
              const label l = text(line.empty() ? ustring(u" ") : line, small, wrap_width, text_style_bits);
              v.lines.push_back(l);
              if (l.width > width) width = l.width;
              height += l.height;
            }
          }
          v.w = std::max(width + 2 * kPadX, 56.0);
          v.h = height;
        }
      }
      if (!c) c = compound_node(v.w, v.h);
      // The transitions on one side of a state's box spread along it, clear of its corners.
      if (v.k == K_State || v.k == K_Described) c->span = (across(i) ? v.w : v.h) - 24;
      cnodes.push_back(*c);
    }

    std::vector<compound_edge> cedges;
    std::vector<label> edge_labels;
    std::vector<const state_edge*> drawn;
    // The transitions each state makes to itself: how many, their widest label, and their labels' height.
    std::vector<int> loop_count(states, 0);
    std::vector<double> loop_width(states, 0);
    std::vector<double> loop_height(states, 0);
    // A transition between a composite and a state inside it runs between that state and a point
    // just inside the composite, and is then drawn on to the composite's border.
    std::map<std::size_t, int> anchors;
    const auto encloses = [&](int composite, int item) {
      if (!cnodes[static_cast<std::size_t>(composite)].is_group) return false;
      for (int p = logical[static_cast<std::size_t>(item)]; p != -1; p = logical[static_cast<std::size_t>(p)])
        if (p == composite) return true;
      return false;
    };
    const auto anchor = [&](int composite, bool first) -> int {
      anchors[cedges.size()] = composite;
      appear.push_back(first ? -1 : infinity);
      const compound_node hidden(0, 0, composite);
      return static_cast<int>(add(nullptr, K_Hidden, &hidden));
    };
    for (const state_edge& edge : graph.edges) {
      const auto src_it = index.find(edge.start);
      const auto dst_it = index.find(edge.end);
      if (src_it == index.end() || dst_it == index.end()) continue;
      int src = static_cast<int>(src_it->second);
      int dst = static_cast<int>(dst_it->second);
      const label l = text(edge.label, small, wrap_width);
      const double label_w = l.width > 0 ? l.width + 12 : 0;
      if (src == dst) {
        loop_count[static_cast<std::size_t>(src)]++;
        loop_width[static_cast<std::size_t>(src)] = std::max(loop_width[static_cast<std::size_t>(src)], label_w);
        loop_height[static_cast<std::size_t>(src)] += l.height;
      } else if (encloses(src, dst)) {
        src = anchor(src, true);
      } else if (encloses(dst, src)) {
        dst = anchor(dst, false);
      }
      drawn.push_back(&edge);
      edge_labels.push_back(l);
      compound_edge ce;
      ce.src = src;
      ce.dst = dst;
      ce.minlen = 1;
      ce.label_w = label_w;
      ce.label_h = l.height > 0 ? l.height + 4 : 0;
      cedges.push_back(std::move(ce));
    }

    // A state with notes, or a composite with self-transitions, sits in an unseen group of its
    // own: the notes go beside the state, across the flow, without disturbing its transitions.
    for (std::size_t i = 0; i < states; i++) {
      if (owners[i] != -1) continue;
      compound_node& item = cnodes[i];
      // Loops hang off the right, or below when the flow is horizontal, with their labels beyond them.
      double room = 0;
      if (loop_count[i] > 0) {
        const double reach = kLoop + (loop_count[i] - 1) * 8;
        room = across(i) ? reach + loop_width[i] : reach * 0.75 + 4 + loop_height[i];
        if (!item.is_group) {
          if (across(i)) {
            item.w += 2 * room;
            item.h = std::max(item.h, loop_height[i]);
          } else {
            item.h += 2 * room;
            item.w = std::max(item.w, loop_width[i]);
          }
          room = 0;
        }
      }
      if (!noted[i] && room == 0) {
        item.parent = logical[i];
        continue;
      }
      compound_node c = across(i) ? group(flow_dir::lr, room, 0, 0) : group(flow_dir::tb, 0, room, room);
      c.parent = logical[i];
      appear.push_back(appear[i]);
      const std::size_t made = add(nullptr, K_Hidden, &c);
      cnodes[i].parent = static_cast<int>(made);
    }
    for (std::size_t i = 0; i < states; i++)
      if (owners[i] != -1) cnodes[i].parent = cnodes[static_cast<std::size_t>(owners[i])].parent;

    {
      // Numbers the tree so that, at every level, initial states come first and the rest follow
      // in the order the transitions introduce them.
      std::vector<std::vector<int>> members(cnodes.size());
      std::vector<int> top;
      for (std::size_t i = 0; i < cnodes.size(); i++) {
        const int p = cnodes[i].parent;
        if (p >= 0) members[static_cast<std::size_t>(p)].push_back(static_cast<int>(i));
        else top.push_back(static_cast<int>(i));
      }
      const auto initial = [&](int i) { return views[static_cast<std::size_t>(i)].k == K_Start ? 0 : 1; };
      const auto by_flow = [&](int a, int b) {
        const int first = initial(a) - initial(b);
        if (first != 0) return first < 0;
        const double pa = appear[static_cast<std::size_t>(a)];
        const double pb = appear[static_cast<std::size_t>(b)];
        if (pa != pb) return pa < pb;
        return a < b;
      };
      double seq = 0;
      std::stable_sort(top.begin(), top.end(), by_flow);
      std::vector<int> pending(top.rbegin(), top.rend());
      while (!pending.empty()) {
        const int i = pending.back();
        pending.pop_back();
        cnodes[static_cast<std::size_t>(i)].seq = seq++;
        std::vector<int>& inside = members[static_cast<std::size_t>(i)];
        std::stable_sort(inside.begin(), inside.end(), by_flow);
        for (std::size_t k = inside.size(); k-- > 0;) pending.push_back(inside[k]);
      }
    }

    layered_options layout_options;
    layout_options.node_sep = number_option(conf, "nodeSpacing", kNodeSep);
    layout_options.edge_sep = kEdgeSep;
    layout_options.rank_sep = number_option(conf, "rankSpacing", kRankSep);
    layout_options.port_sep = 20;
    layout_options.tracks = has_curve && (curve == "step" || curve == "stepBefore" || curve == "stepAfter");
    layout_options.straight = has_curve && curve == "linear";
    layout_options.head_room = edge_geometry::marker_trim("arrow_point");
    const layered_layouter engine(layout_options);
    const compound_result layout = compound_layouter(engine).layout(cnodes, cedges, root_dir);

    const double width = std::ceil(layout.width + 2 * pad);
    const double height = std::ceil(layout.height + 2 * pad);
    std::vector<rendered_link> links;

    // Route points by rounded y, to find the edges that cross a composite's title bar.
    std::map<double, std::vector<double>> crossings;
    for (const compound_edge& e : cedges) {
      const std::vector<double>& route = e.route;
      for (std::size_t k = 3; k + 3 < route.size(); k += 3) crossings[js_round(route[k + 1])].push_back(route[k]);
    }

    std::vector<std::vector<int>> children(cnodes.size());
    std::vector<int> order;
    for (std::size_t i = 0; i < cnodes.size(); i++) {
      const int p = cnodes[i].parent;
      if (p >= 0) children[static_cast<std::size_t>(p)].push_back(static_cast<int>(i));
      else order.push_back(static_cast<int>(i));
    }
    const std::vector<int> roots = order;
    for (std::size_t q = 0; q < order.size(); q++)
      for (const int child : children[static_cast<std::size_t>(order[q])]) order.push_back(child);

    std::string clusters;
    std::vector<edge_geometry::fill> fills;
    std::string dividers;
    std::string nodes_out;
    std::string notes_out;
    // Draws the dashed lines between the regions among a composite's members.
    const auto divide = [&](const std::vector<int>& items, double left, double top, double right, double bottom) {
      std::vector<int> regions;
      for (const int i : items)
        if (views[static_cast<std::size_t>(i)].k == K_Region) regions.push_back(i);
      if (regions.size() < 2) return;
      const auto lo = [&](int i, bool x) { return x ? cnodes[static_cast<std::size_t>(i)].x - cnodes[static_cast<std::size_t>(i)].w / 2 : cnodes[static_cast<std::size_t>(i)].y - cnodes[static_cast<std::size_t>(i)].h / 2; };
      const auto hi = [&](int i, bool x) { return x ? cnodes[static_cast<std::size_t>(i)].x + cnodes[static_cast<std::size_t>(i)].w / 2 : cnodes[static_cast<std::size_t>(i)].y + cnodes[static_cast<std::size_t>(i)].h / 2; };
      for (const bool x : {true, false}) {
        std::vector<int> sorted = regions;
        std::stable_sort(sorted.begin(), sorted.end(), [&](int a, int b) {
          const double d = lo(a, x) - lo(b, x);
          if (d != 0) return d < 0;
          return a < b;
        });
        bool apart = true;
        for (std::size_t k = 1; k < sorted.size(); k++)
          if (hi(sorted[k - 1], x) > lo(sorted[k], x) + 0.5) apart = false;
        if (!apart) continue;
        for (std::size_t k = 1; k < sorted.size(); k++) {
          const std::string at = num((hi(sorted[k - 1], x) + lo(sorted[k], x)) / 2 + pad);
          dividers += "<path class=\"pele-divider\" d=\"M" + (x ? at + "," + num(top) + "V" + num(bottom) : num(left) + "," + at + "H" + num(right)) + "\"/>";
        }
        return;
      }
      // Regions that sit neither side by side nor in a stack each get an outline.
      for (const int i : regions) {
        const compound_node& c = cnodes[static_cast<std::size_t>(i)];
        dividers += rect(c.x - c.w / 2 + pad, c.y - c.h / 2 + pad, c.w, c.h, " class=\"pele-divider\" rx=\"" + num(kCorner) + "\"");
      }
    };
    divide(roots, pad / 2, pad / 2, width - pad / 2, height - pad / 2);

    for (const int idx_i : order) {
      const std::size_t i = static_cast<std::size_t>(idx_i);
      view& v = views[i];
      const state_node* node = v.node;
      if (node == nullptr || !v.style || v.k >= K_Region) continue;
      const theme::resolved_style& style = *v.style;
      const compound_node& c = cnodes[i];
      const double x = c.x + pad;
      const double y = c.y + pad;
      const double w = v.w;
      const double h = v.h;
      const std::string ink = ink_fill(style);
      const std::string label_attrs = " class=\"pele-label\"" + ink + style.text;
      std::string body;

      switch (v.k) {
        case K_Composite: {
          const double left = x - c.w / 2;
          const double top = y - c.h / 2;
          if (paints(style.shape)) fills.push_back({{left, top, left + c.w, top + c.h}, style.shape});
          body = rect(left, top, c.w, c.h, " rx=\"" + num(kCorner) + "\" fill=\"var(--_a)\"" + (theme::sets_fill(style.shape) ? "" : " fill-opacity=\"0.5\"") + " stroke=\"var(--_b)\"" + style.shape);
          if (v.title_h > 0) {
            body += "<path d=\"M" + num(left) + "," + num(top + v.title_h) + "H" + num(left + c.w) + "\"" + kRule + style.line + "/>" +
                    svg::label_svg(v.text, title_x(c, v.text.width, crossings) + pad, top + v.title_h / 2, " class=\"pele-cluster-label\"" + ink + style.text, &icons_);
          }
          divide(children[i], left, top + v.title_h, left + c.w, top + c.h);
          break;
        }
        case K_Start:
          body = "<circle r=\"7\" fill=\"var(--_l)\"" + style.shape + "/>";
          break;
        case K_End:
          body = "<circle r=\"8.5\" fill=\"var(--_bg)\" stroke=\"var(--_l)\"" + style.line + "/><circle r=\"5\" fill=\"var(--_l)\"" + style.shape + "/>";
          break;
        case K_Choice:
          body = "<polygon points=\"0," + num(-h / 2) + " " + num(w / 2) + ",0 0," + num(h / 2) + " " + num(-w / 2) + ",0\"" + kSurface + style.shape + "/>";
          break;
        case K_Fork:
        case K_Join:
          body = rect(-w / 2, -h / 2, w, h, " rx=\"2\" fill=\"var(--_l)\"" + style.shape);
          break;
        case K_Note: {
          const std::string r = num(w / 2);
          const std::string b = num(h / 2);
          const std::string fold = num(w / 2 - kFold);
          const std::string corner = num(kFold - h / 2);
          body = "<path d=\"M-" + r + ",-" + b + "H" + fold + "L" + r + "," + corner + "V" + b + "H-" + r + "Z\" fill=\"var(--_bg)\" stroke=\"var(--_b)\"/>" + "<path d=\"M" + fold + ",-" + b + "V" + corner +
                 "H" + r + "\"" + kRule + "/>" + svg::label_svg(v.text, 0, 0, " class=\"pele-label\" font-size=\"" + js_number_string(small) + "\" fill=\"var(--_m)\"", &icons_);
          break;
        }
        default: {
          body = rect(-w / 2, -h / 2, w, h, " rx=\"" + num(std::min(kCorner, h / 2)) + "\"" + kSurface + style.shape);
          if (v.k == K_State) {
            body += svg::label_svg(v.text, 0, 0, label_attrs, &icons_);
            break;
          }
          double at = kPadY - h / 2 + v.text.height;
          body += "<path d=\"M" + num(-w / 2) + "," + num(at + 6) + "H" + num(w / 2) + "\"" + kRule + style.line + "/>" + svg::label_svg(v.text, 0, at - v.text.height / 2, label_attrs, &icons_);
          at += 14;
          for (const label& line : v.lines) {
            body += svg::label_svg(line, 0, at + line.height / 2, " class=\"pele-state-description\" font-size=\"" + js_number_string(small) + "\"" + ink + style.text, &icons_);
            at += line.height;
          }
        }
      }

      const auto link = db_.links.find(node->id);
      if (link != db_.links.end()) {
        // Entity codes are resolved first, so the URL is checked in the form a browser would follow.
        const ustring tooltip = decode_entities(unquote(link->second.tooltip));
        if (!tooltip.empty()) body = "<title>" + esc(tooltip) + "</title>" + body;
        body = linked(body, safe_url(unquote(link->second.url)), node->id, links, options_, " target=\"_blank\"");
      }
      const std::string open = std::string("<g class=\"") + kClasses[v.k] + theme::class_names(strip_mermaid_classes(node->css_classes)) + "\" data-id=\"" + esc_text(node->id) + "\"";
      if (v.k == K_Composite) clusters += open + ">" + body + "</g>";
      else if (v.k == K_Note) notes_out += open + " transform=\"translate(" + num(x) + "," + num(y) + ")\">" + body + "</g>";
      else nodes_out += open + " transform=\"translate(" + num(x) + "," + num(y) + ")\">" + body + "</g>";
    }

    // Moves a route end from the layout box onto the drawn shape, and spreads the ends along a bar.
    const auto attach = [&](std::vector<double>& route, std::size_t at, std::size_t i) {
      const compound_node& c = cnodes[i];
      if (c.is_group) return;
      const view& v = views[i];
      const bool bar = v.k == K_Fork || v.k == K_Join;
      const std::size_t other = at == 0 ? 3 : at - 3;
      // 0 is the x axis and 1 the y axis; the end sits on a side that faces along `axis`.
      for (const int axis : {1, 0}) {
        const double extent = axis ? c.h : c.w;
        const double drawn_extent = axis ? v.h : v.w;
        const double breadth = axis ? v.w : v.h;
        const double center = (axis ? c.y : c.x) + pad;
        const double cross = (axis ? c.x : c.y) + pad;
        const double delta = route[at + static_cast<std::size_t>(axis)] - center;
        if (std::abs(std::abs(delta) - extent / 2) >= 0.5) continue;
        route[at + static_cast<std::size_t>(axis)] -= (js_sign(delta) * (extent - drawn_extent)) / 2;
        if (bar && breadth > drawn_extent) {
          route[at + static_cast<std::size_t>(1 - axis)] = clamp(route[other + static_cast<std::size_t>(1 - axis)], cross - breadth / 2 + 6, cross + breadth / 2 - 6);
        }
        return;
      }
    };

    side_reach reach;
    for (std::size_t i = 0; i < cedges.size(); i++) {
      const compound_edge& ce = cedges[i];
      if (ce.src == ce.dst || ce.route.size() < 6 || drawn[i]->dashed) continue;
      reach.add(ce.dst, end_side(ce.route, ce.route.size() - 3, cnodes[static_cast<std::size_t>(ce.dst)]), "arrow_point");
    }

    std::string casings;
    std::string edges_out;
    std::string labels_out;
    std::vector<int> loop_seen(states, 0);
    std::vector<double> loop_text(states, 0);
    for (std::size_t i = 0; i < cedges.size(); i++) {
      compound_edge& ce = cedges[i];
      const state_edge& edge = *drawn[i];
      const label& l = edge_labels[i];
      const bool note = edge.dashed;
      const std::string end_type = note ? "none" : "arrow_point";
      edge_path path;
      if (ce.src == ce.dst) {
        const std::size_t s = static_cast<std::size_t>(ce.src);
        const compound_node& a = cnodes[s];
        const view& v = views[s];
        const int k = loop_seen[s]++;
        // The loop hangs off a side the flow does not use: the right, or below when the flow is horizontal.
        const bool side = across(s);
        const double w = a.is_group ? a.w : v.w;
        const double h = a.is_group ? a.h : v.h;
        const double out = (side ? w : h) / 2;
        const double spread = std::max(std::min((side ? h : w) / 2 - 4, 8.0 + k * 6), 2.0);
        const double reach_k = kLoop + k * 8;
        const double hyp = js_hypot(reach_k, spread);
        const double trim = edge_geometry::marker_trim(end_type) / (hyp != 0 ? hyp : 1);
        // Offsets are given for a loop on the right and turned a quarter for one below.
        const auto px = [&](double dx, double dy) { return a.x + pad + (side ? out + dx : -dy); };
        const auto py = [&](double dx, double dy) { return a.y + pad + (side ? dy : out + dx); };
        const auto at = [&](double dx, double dy) { return num(px(dx, dy)) + "," + num(py(dx, dy)); };
        path.d = "M" + at(0, -spread) + "C" + at(reach_k, -2 * spread) + " " + at(reach_k, 2 * spread) + " " + at(reach_k * trim, spread + spread * trim);
        path.ex = px(0, spread);
        path.ey = py(0, spread);
        path.edx = px(-reach_k, -spread) - px(0, 0);
        path.edy = py(-reach_k, -spread) - py(0, 0);
        const double len = js_hypot(path.edx, path.edy);
        const double norm = len != 0 ? len : 1;
        path.edx /= norm;
        path.edy /= norm;
        // Labels stack beyond the outermost loop, in the order of their transitions.
        const double furthest = kLoop + (loop_count[s] - 1) * 8;
        const double below = loop_text[s] + l.height / 2;
        loop_text[s] += l.height;
        ce.label_x = side ? a.x + out + furthest + 4 + ce.label_w / 2 : a.x;
        ce.label_y = side ? a.y - loop_height[s] / 2 + below : a.y + out + furthest * 0.75 + 4 + below;
      } else {
        std::vector<double> route = ce.route;
        const double leaves = reach.of(ce.src, end_side(route, 0, cnodes[static_cast<std::size_t>(ce.src)]));
        const double arrives = reach.of(ce.dst, end_side(route, route.size() - 3, cnodes[static_cast<std::size_t>(ce.dst)]));
        for (std::size_t k = 0; k + 1 < route.size(); k += 3) {
          route[k] += pad;
          route[k + 1] += pad;
        }
        attach(route, 0, static_cast<std::size_t>(ce.src));
        attach(route, route.size() - 3, static_cast<std::size_t>(ce.dst));
        const auto composite = anchors.find(i);
        if (composite != anchors.end() && route.size() >= 6) {
          const std::size_t gi = static_cast<std::size_t>(composite->second);
          const compound_node& g = cnodes[gi];
          const bool inward = views[static_cast<std::size_t>(ce.src)].k == K_Hidden;
          const std::size_t at = inward ? 0 : route.size() - 3;
          const bool near = (g.dir == flow_dir::tb || g.dir == flow_dir::lr) == inward;
          std::vector<double> point;
          if (vertical(g.dir)) point = {route[at], g.y + pad + (near ? views[gi].title_h - g.h / 2 : g.h / 2), 0};
          else point = {g.x + pad + (near ? -g.w / 2 : g.w / 2), route[at + 1], 1};
          if (inward) {
            point.insert(point.end(), route.begin(), route.end());
            route = std::move(point);
          } else {
            route.insert(route.end(), point.begin(), point.end());
          }
        }
        path = edge_geometry::route_path(route, has_curve ? curve : std::string(), 0, edge_geometry::marker_trim(end_type), leaves, arrives);
      }
      const std::optional<std::string> over = edge_geometry::over_fills(path.d, {path.ex, path.ey}, fills, "", path.tokens);
      if (over && over->empty()) casings += edge_geometry::edge_casing(path, "none", end_type);
      const bool has_over = over && !over->empty();
      const std::string edge_kind = note ? "pele-note-edge\" stroke=\"" + (has_over ? *over : std::string("var(--_b)")) + "\" stroke-dasharray=\"3 4"
                                         : has_over ? "pele-transition\" stroke=\"" + *over : std::string("pele-transition");
      edges_out += "<g class=\"pele-edge " + edge_kind + "\" data-id=\"" + esc_text(edge.id) + "\">" + "<path d=\"" + path.d + "\"/>" +
                   edge_geometry::marker(end_type, path.ex, path.ey, path.edx, path.edy, has_over ? *over : std::string("var(--_l)")) + "</g>";
      if (l.width > 0) {
        const double x = ce.label_x + pad;
        const double y = ce.label_y + pad;
        labels_out += edge_geometry::label_svg(to_utf8(edge.id), l, x, y, "", &icons_);
      }
    }

    const std::string content = (clusters.empty() ? "" : "<g class=\"pele-clusters\">" + clusters + "</g>") +
                                (dividers.empty() ? "" : "<g class=\"pele-dividers\" stroke-dasharray=\"4 4\"" + std::string(kRule) + ">" + dividers + "</g>") + edge_geometry::casings_svg(casings) +
                                (edges_out.empty() ? "" : "<g class=\"pele-edges\" fill=\"none\" stroke=\"var(--_l)\" stroke-linecap=\"round\">" + edges_out + "</g>") +
                                (labels_out.empty() ? "" : "<g class=\"pele-edge-labels\" font-size=\"" + js_number_string(small) + "\">" + labels_out + "</g>") +
                                "<g class=\"pele-nodes\">" + nodes_out + notes_out + "</g>";
    const titled t = with_title(db_.title ? ustring_view(*db_.title) : ustring_view(), layouter, size, pad, width, height, content);
    accessible acc;
    if (db_.acc_title) acc.title = *db_.acc_title;
    if (db_.acc_descr) acc.description = *db_.acc_descr;
    rendered result;
    result.svg = svg_document::build("state", t.width, t.height, size, options_, acc, t.content);
    result.width = t.width;
    result.height = t.height;
    result.links = std::move(links);
    return result;
  }

  const state_model& db_;
  const config& settings_;
  const render_options& options_;
  icon_set icons_;
};

}  // namespace

rendered render_state(const state_model& model, const config& settings, const render_options& options) { return state_renderer(model, settings, options).render(); }

}  // namespace pele::state
