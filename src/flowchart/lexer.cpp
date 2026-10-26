#include "lexer.h"

#include <array>
#include <cstdlib>

#include "chars.h"
#include "unicode_letter.h"

namespace pele::flowchart {
namespace {

enum state : int {
  S_INITIAL, S_string, S_md_string, S_acc_title, S_acc_descr, S_acc_descr_multiline, S_dir, S_text, S_ellipseText, S_trapText, S_edgeText, S_thickEdgeText,
  S_dottedEdgeText, S_click, S_callbackname, S_callbackargs, S_shapeData, S_shapeDataStr
};

// The characters of a node name, besides letters and digits.
const std::array<bool, 128>& node_chars() {
  static const std::array<bool, 128> table = [] {
    std::array<bool, 128> t{};
    for (const char* c = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"#$%&'*+.`?\\_/"; *c; c++) t[static_cast<unsigned char>(*c)] = true;
    return t;
  }();
  return table;
}

// Which of the sticky patterns an edge-text state looks for to end its text.
enum class link_kind { normal, thick, dotted };

class lexer {
 public:
  explicit lexer(const ustring& src) : src_(src), n_(src.size()) {
    dir_next_ = find(u"direction", 0);
    at_next_ = find(u"@", 0);
  }

  token_list run() {
    while (true) {
      if (p_ >= n_) {
        int type = -1;
        switch (state_) {
          case S_INITIAL: type = T_EOF; break;
          case S_acc_title: type = T_acc_title_value; break;
          case S_acc_descr: type = T_acc_descr_value; break;
          case S_acc_descr_multiline: type = T_acc_descr_multiline_value; break;
          case S_click: type = T_CLICK; break;
          case S_callbackname: type = T_CALLBACKNAME; break;
          case S_callbackargs: type = T_CALLBACKARGS; break;
          default: break;
        }
        if (type != -1) emit(type, n_, n_);
        break;
      }
      const int c = src_[p_];
      bool ok = true;
      switch (state_) {
        case S_INITIAL: ok = initial(c); break;
        case S_string: lex_string(c); break;
        case S_md_string: ok = lex_md_string(c); break;
        case S_acc_title:
        case S_acc_descr: {
          std::size_t e = src_.find(u'\n', p_);
          if (e == ustring::npos) e = n_;
          const int type = state_ == S_acc_title ? T_acc_title_value : T_acc_descr_value;
          pop();
          emit(type, p_, e);
          break;
        }
        case S_acc_descr_multiline:
          if (c == 125) {
            pop();
            p_++;
          } else {
            std::size_t e = src_.find(u'}', p_);
            if (e == ustring::npos) e = n_;
            emit(T_acc_descr_multiline_value, p_, e);
          }
          break;
        case S_dir: ok = lex_dir(c); break;
        case S_text: lex_text(c); break;
        case S_ellipseText: ok = lex_ellipse_text(c); break;
        case S_trapText: ok = lex_trap_text(c); break;
        case S_edgeText: ok = edge_text(c, link_kind::normal, 45, -1); break;
        case S_thickEdgeText: ok = edge_text(c, link_kind::thick, 61, -1); break;
        case S_dottedEdgeText: ok = edge_text(c, link_kind::dotted, 46, 45); break;
        case S_click: lex_click(c); break;
        case S_callbackname: lex_callbackname(c); break;
        case S_callbackargs:
          if (c == 41) {
            pop();
            p_++;
          } else {
            std::size_t e = src_.find(u')', p_);
            if (e == ustring::npos) e = n_;
            emit(T_CALLBACKARGS, p_, e);
          }
          break;
        case S_shapeData: ok = lex_shape_data(c); break;
        case S_shapeDataStr: lex_shape_data_str(c); break;
        default: break;
      }
      if (!ok) {
        out_.types.push_back(T_ERROR);
        out_.texts.emplace_back();
        out_.starts.push_back(p_);
        return std::move(out_);
      }
    }
    out_.types.push_back(T_END);
    out_.texts.emplace_back();
    out_.starts.push_back(n_);
    return std::move(out_);
  }

 private:
  using index = long;

  int at(std::size_t i) const { return i < n_ ? src_[i] : -1; }
  index find(const char16_t* needle, std::size_t from) const {
    const std::size_t f = src_.find(needle, from);
    return f == ustring::npos ? -1 : static_cast<index>(f);
  }

  void emit(int type, std::size_t s, std::size_t e) {
    out_.types.push_back(type);
    out_.texts.push_back(src_.substr(s, e - s));
    out_.starts.push_back(s);
    p_ = e;
  }
  void push_state(int s) {
    stack_.push_back(state_);
    state_ = s;
  }
  void pop() {
    if (!stack_.empty()) {
      state_ = stack_.back();
      stack_.pop_back();
    }
  }
  bool kw(const char* word) const {
    std::size_t len = 0;
    while (word[len]) len++;
    for (std::size_t k = 0; k < len; k++)
      if (at(p_ + k) != static_cast<unsigned char>(word[k])) return false;
    return !is_word(at(p_ + len));
  }
  bool lit(std::size_t q, const char* word) const {
    for (std::size_t k = 0; word[k]; k++)
      if (at(q + k) != static_cast<unsigned char>(word[k])) return false;
    return true;
  }
  std::size_t skip_ws(std::size_t q) const {
    while (q < n_ && is_space(src_[q])) q++;
    return q;
  }

  // ---- the sticky patterns; each gives the end of its match at q, or -1 ----

  // `word\s*punct\s*`
  index m_word_punct(std::size_t q, const char* word, int punct) const {
    if (!lit(q, word)) return -1;
    std::size_t len = 0;
    while (word[len]) len++;
    const std::size_t k = skip_ws(q + len);
    if (at(k) != punct) return -1;
    return static_cast<index>(skip_ws(k + 1));
  }
  // `word\s+`
  index m_word_space(std::size_t q, const char* word) const {
    if (!lit(q, word)) return -1;
    std::size_t len = 0;
    while (word[len]) len++;
    const std::size_t k = skip_ws(q + len);
    return k > q + len ? static_cast<index>(k) : -1;
  }
  // `(?:\r?\n)*\s*\n`: up to and including the last line feed of the whitespace that starts here.
  index m_nodir(std::size_t q) const {
    const std::size_t r = skip_ws(q);
    for (std::size_t k = r; k > q; k--)
      if (src_[k - 1] == '\n') return static_cast<index>(k);
    return -1;
  }
  // `\s*(?:LR|RL|TB|BT|TD|BR)\b`
  index m_dir(std::size_t q) const {
    const std::size_t k = skip_ws(q);
    const int a = at(k);
    const int b = at(k + 1);
    const bool known = (a == 'L' && b == 'R') || (a == 'R' && b == 'L') || (a == 'T' && b == 'B') || (a == 'B' && b == 'T') || (a == 'T' && b == 'D') || (a == 'B' && b == 'R');
    if (!known || is_word(at(k + 2))) return -1;
    return static_cast<index>(k + 2);
  }
  index m_dir_sym(std::size_t q) const {
    const std::size_t k = skip_ws(q);
    const int a = at(k);
    return a == '<' || a == '>' || a == '^' ? static_cast<index>(k + 1) : -1;
  }
  index m_dir_v(std::size_t q) const {
    const std::size_t k = skip_ws(q);
    return at(k) == 'v' && !is_word(at(k + 1)) ? static_cast<index>(k + 1) : -1;
  }
  // `.*direction\s+XX[^\n]*`, where .* backtracks to the last `direction` on the line.
  index m_direction(std::size_t q, const char* dir) const {
    std::size_t line_end = q;
    while (line_end < n_ && !is_line_end(src_[line_end])) line_end++;
    std::size_t k = src_.rfind(u"direction", line_end == 0 ? 0 : line_end - 1);
    while (k != ustring::npos && k >= q) {
      if (k + 9 <= line_end) {
        const std::size_t w = skip_ws(k + 9);
        if (w > k + 9 && at(w) == static_cast<unsigned char>(dir[0]) && at(w + 1) == static_cast<unsigned char>(dir[1])) {
          std::size_t e = w + 2;
          while (e < n_ && src_[e] != '\n') e++;
          return static_cast<index>(e);
        }
      }
      if (k == 0) break;
      k = src_.rfind(u"direction", k - 1);
    }
    return -1;
  }
  // The position after `\s*[xo<]?`.
  std::size_t after_prefix(std::size_t q) const {
    std::size_t k = skip_ws(q);
    const int c = at(k);
    if (c == 'x' || c == 'o' || c == '<') k++;
    return k;
  }
  // `\s*[xo<]?--+[-xo>]\s*` (and the thick form with `=`)
  index m_link(std::size_t q, int ch) const {
    const std::size_t k = after_prefix(q);
    std::size_t d = 0;
    while (at(k + d) == ch) d++;
    if (d < 2) return -1;
    const int next = at(k + d);
    std::size_t e;
    if (next == 'x' || next == 'o' || next == '>') e = k + d + 1;
    else if (d >= 3) e = k + d;
    else return -1;
    return static_cast<index>(skip_ws(e));
  }
  // `\s*[xo<]?--\s*` (and the thick form)
  index m_start_link(std::size_t q, int ch) const {
    const std::size_t k = after_prefix(q);
    if (at(k) != ch || at(k + 1) != ch) return -1;
    return static_cast<index>(skip_ws(k + 2));
  }
  // `\s*[xo<]?-?\.+-[xo>]?\s*`
  index m_dotted_link(std::size_t q) const {
    std::size_t j = after_prefix(q);
    if (at(j) == '-') j++;
    std::size_t dots = 0;
    while (at(j + dots) == '.') dots++;
    if (dots == 0 || at(j + dots) != '-') return -1;
    std::size_t e = j + dots + 1;
    const int c = at(e);
    if (c == 'x' || c == 'o' || c == '>') e++;
    return static_cast<index>(skip_ws(e));
  }
  // `\s*[xo<]?-\.\s*`
  index m_dotted_start(std::size_t q) const {
    const std::size_t k = after_prefix(q);
    if (at(k) != '-' || at(k + 1) != '.') return -1;
    return static_cast<index>(skip_ws(k + 2));
  }
  // `\s*~~~+\s*`
  index m_invis(std::size_t q) const {
    const std::size_t k = skip_ws(q);
    std::size_t d = 0;
    while (at(k + d) == '~') d++;
    if (d < 3) return -1;
    return static_cast<index>(skip_ws(k + d));
  }
  index m_link_of(link_kind kind, std::size_t q) const {
    switch (kind) {
      case link_kind::normal: return m_link(q, '-');
      case link_kind::thick: return m_link(q, '=');
      default: return m_dotted_link(q);
    }
  }

  // First character after any run of whitespace starting at q, skipping one optional x, o or <.
  int link_char(std::size_t q) {
    std::size_t j = q;
    if (static_cast<index>(q) >= ws_from_ && static_cast<index>(q) <= ws_to_) {
      j = static_cast<std::size_t>(ws_to_);
    } else if (is_space(at(q))) {
      while (j < n_ && is_space(src_[j])) j++;
      ws_from_ = static_cast<index>(q);
      ws_to_ = static_cast<index>(j);
    }
    const int d = at(j);
    return d == 120 || d == 111 || d == 60 ? at(j + 1) : d;
  }
  bool invis_ahead(std::size_t q) {
    std::size_t j = q;
    if (static_cast<index>(q) >= ws_from_ && static_cast<index>(q) <= ws_to_) {
      j = static_cast<std::size_t>(ws_to_);
    } else if (is_space(at(q))) {
      while (j < n_ && is_space(src_[j])) j++;
      ws_from_ = static_cast<index>(q);
      ws_to_ = static_cast<index>(j);
    }
    return at(j) == 126;
  }

  void quote() {
    if (at(p_ + 1) == 96) {
      push_state(S_md_string);
      p_ += 2;
    } else {
      push_state(S_string);
      p_++;
    }
  }

  bool invis_link() {
    if (!invis_ahead(p_)) return false;
    const index e = m_invis(p_);
    if (e < 0) return false;
    emit(T_LINK, p_, static_cast<std::size_t>(e));
    return true;
  }

  bool opener(int c) {
    const int d = at(p_ + 1);
    if (c == 40) {
      if (d == 45) {
        push_state(S_ellipseText);
        emit(T_ELLIPSE_START, p_, p_ + 2);
      } else if (d == 91) {
        push_state(S_text);
        emit(T_STADIUMSTART, p_, p_ + 2);
      } else if (d == 40 && at(p_ + 2) == 40) {
        push_state(S_text);
        emit(T_DOUBLECIRCLESTART, p_, p_ + 3);
      } else {
        push_state(S_text);
        emit(T_PS, p_, p_ + 1);
      }
      return true;
    }
    if (c == 91) {
      if (d == 91) {
        push_state(S_text);
        emit(T_SUBROUTINESTART, p_, p_ + 2);
      } else if (d == 40) {
        push_state(S_text);
        emit(T_CYLINDERSTART, p_, p_ + 2);
      } else if (d == 47) {
        push_state(S_trapText);
        emit(T_TRAPSTART, p_, p_ + 2);
      } else if (d == 92) {
        push_state(S_trapText);
        emit(T_INVTRAPSTART, p_, p_ + 2);
      } else {
        push_state(S_text);
        emit(T_SQS, p_, p_ + 1);
      }
      return true;
    }
    if (c == 124) {
      push_state(S_text);
      emit(T_PIPE, p_, p_ + 1);
      return true;
    }
    if (c == 123) {
      push_state(S_text);
      emit(T_DIAMOND_START, p_, p_ + 1);
      return true;
    }
    return false;
  }

  void graph(std::size_t len) {
    if (first_graph_) {
      first_graph_ = false;
      push_state(S_dir);
    }
    emit(T_GRAPH, p_, p_ + len);
  }

  bool node_string() {
    std::size_t q = p_;
    while (q < n_) {
      const int ch = src_[q];
      if (ch < 128 && node_chars()[ch]) {
        q++;
      } else if (ch == 45 && q + 1 < n_) {
        const int d = src_[q + 1];
        if (d == 62 || d == 45 || d == 46) break;
        q++;
      } else {
        break;
      }
    }
    if (q == p_) return false;
    emit(T_NODE_STRING, p_, q);
    return true;
  }

  // The link patterns for what starts here, then, where the character cannot itself start one, an invisible link.
  bool link(int c) {
    if (link_patterns()) return true;
    return c != 45 && c != 61 && c != 46 && invis_link();
  }

  bool link_patterns() {
    const int d = link_char(p_);
    index e;
    if (d == 45) {
      if ((e = m_link(p_, '-')) >= 0) {
        emit(T_LINK, p_, e);
        return true;
      }
      if ((e = m_start_link(p_, '-')) >= 0) {
        push_state(S_edgeText);
        emit(T_START_LINK, p_, e);
        return true;
      }
      if ((e = m_dotted_link(p_)) >= 0) {
        emit(T_LINK, p_, e);
        return true;
      }
      if ((e = m_dotted_start(p_)) >= 0) {
        push_state(S_dottedEdgeText);
        emit(T_START_LINK, p_, e);
        return true;
      }
    } else if (d == 61) {
      if ((e = m_link(p_, '=')) >= 0) {
        emit(T_LINK, p_, e);
        return true;
      }
      if ((e = m_start_link(p_, '=')) >= 0) {
        push_state(S_thickEdgeText);
        emit(T_START_LINK, p_, e);
        return true;
      }
    } else if (d == 46) {
      if ((e = m_dotted_link(p_)) >= 0) {
        emit(T_LINK, p_, e);
        return true;
      }
      if ((e = m_dotted_start(p_)) >= 0) {
        push_state(S_dottedEdgeText);
        emit(T_START_LINK, p_, e);
        return true;
      }
    }
    return false;
  }

  bool initial(int c) {
    index e;
    switch (c) {
      case 97:
        if (lit(p_, "acc")) {
          if ((e = m_word_punct(p_, "accTitle", ':')) >= 0) {
            push_state(S_acc_title);
            emit(T_acc_title, p_, e);
            return true;
          }
          if ((e = m_word_punct(p_, "accDescr", ':')) >= 0) {
            push_state(S_acc_descr);
            emit(T_acc_descr, p_, e);
            return true;
          }
          if ((e = m_word_punct(p_, "accDescr", '{')) >= 0) {
            push_state(S_acc_descr_multiline);
            p_ = e;
            return true;
          }
        }
        break;
      case 64:
        if (at(p_ + 1) == 123) {
          push_state(S_shapeData);
          out_.types.push_back(T_SHAPE_DATA);
          out_.texts.emplace_back();
          out_.starts.push_back(p_);
          p_ += 2;
          return true;
        }
        break;
      case 99:
        if ((e = m_word_space(p_, "call")) >= 0) {
          push_state(S_callbackname);
          p_ = e;
          return true;
        }
        if (kw("classDef")) {
          emit(T_CLASSDEF, p_, p_ + 8);
          return true;
        }
        if (kw("class")) {
          emit(T_CLASS, p_, p_ + 5);
          return true;
        }
        if ((e = m_word_space(p_, "click")) >= 0) {
          push_state(S_click);
          p_ = e;
          return true;
        }
        break;
      case 34:
        quote();
        return true;
      case 115:
        if (kw("style")) {
          emit(T_STYLE, p_, p_ + 5);
          return true;
        }
        if (kw("swimlane-beta")) {
          graph(13);
          return true;
        }
        if (kw("subgraph")) {
          emit(T_subgraph, p_, p_ + 8);
          return true;
        }
        break;
      case 100:
        if (kw("default")) {
          emit(T_DEFAULT, p_, p_ + 7);
          return true;
        }
        break;
      case 108:
        if (kw("linkStyle")) {
          emit(T_LINKSTYLE, p_, p_ + 9);
          return true;
        }
        break;
      case 105:
        if (kw("interpolate")) {
          emit(T_INTERPOLATE, p_, p_ + 11);
          return true;
        }
        break;
      case 104:
        if (lit(p_, "href") && is_space(at(p_ + 4))) {
          emit(T_HREF, p_, p_ + 5);
          return true;
        }
        break;
      case 102:
        if (kw("flowchart-elk")) {
          graph(13);
          return true;
        }
        if (kw("flowchart")) {
          graph(9);
          return true;
        }
        break;
      case 103:
        if (kw("graph")) {
          graph(5);
          return true;
        }
        break;
      case 101:
        if (kw("end")) {
          std::size_t k = p_ + 3;
          while (k < n_ && is_space(src_[k])) k++;
          emit(T_end_kw, p_, k);
          return true;
        }
        break;
      case 95:
        if (kw("_self")) {
          emit(T_LINK_TARGET, p_, p_ + 5);
          return true;
        }
        if (kw("_blank")) {
          emit(T_LINK_TARGET, p_, p_ + 6);
          return true;
        }
        if (kw("_parent")) {
          emit(T_LINK_TARGET, p_, p_ + 7);
          return true;
        }
        if (kw("_top")) {
          emit(T_LINK_TARGET, p_, p_ + 4);
          return true;
        }
        break;
      default:
        break;
    }

    if (dir_next_ != -1 && static_cast<index>(p_) >= dir_failed_) {
      if (dir_next_ < static_cast<index>(p_)) dir_next_ = find(u"direction", p_);
      if (dir_next_ != -1) {
        if (line_end_ < static_cast<index>(p_)) {
          std::size_t k = p_;
          while (k < n_ && !is_line_end(src_[k])) k++;
          line_end_ = static_cast<index>(k);
        }
        if (dir_next_ < line_end_) {
          static const char* const kDirs[5] = {"TB", "BT", "RL", "LR", "TD"};
          for (int i = 0; i < 5; i++) {
            if ((e = m_direction(p_, kDirs[i])) >= 0) {
              emit(T_direction_tb + i, p_, e);
              return true;
            }
          }
          // No direction statement starts anywhere in the rest of this line either.
          dir_failed_ = line_end_;
        }
      }
    }

    // A link id is a run of non-space characters up to its last `@` that is not followed by `{` or `"`.
    if (at_next_ != -1 && c != 34 && !is_space(c)) {
      if (static_cast<index>(p_) >= run_end_) {
        if (at_next_ < static_cast<index>(p_)) at_next_ = find(u"@", p_);
        std::size_t q = p_;
        while (q < n_) {
          const int ch = src_[q];
          if (ch == 34 || is_space(ch)) break;
          q++;
        }
        run_end_ = static_cast<index>(q);
        run_at_ = -1;
        if (at_next_ != -1 && at_next_ < static_cast<index>(q)) {
          for (index k = static_cast<index>(std::min(q, n_ - 1)) - 1; k >= at_next_; k--) {
            if (src_[k] != 64) continue;
            const int after = at(static_cast<std::size_t>(k) + 1);
            if (after != 123 && after != 34) {
              run_at_ = k;
              break;
            }
          }
        }
      }
      if (run_at_ > static_cast<index>(p_)) {
        emit(T_LINK_ID, p_, static_cast<std::size_t>(run_at_) + 1);
        return true;
      }
    }

    if (c >= 48 && c <= 57) {
      std::size_t q = p_ + 1;
      while (q < n_) {
        const int ch = src_[q];
        if (ch < 48 || ch > 57) break;
        q++;
      }
      emit(T_NUM, p_, q);
      return true;
    }

    switch (c) {
      case 35:
        emit(T_BRKT, p_, p_ + 1);
        return true;
      case 58:
        if (at(p_ + 1) == 58 && at(p_ + 2) == 58) emit(T_STYLE_SEPARATOR, p_, p_ + 3);
        else emit(T_COLON, p_, p_ + 1);
        return true;
      case 38:
        emit(T_AMP, p_, p_ + 1);
        return true;
      case 59:
        emit(T_SEMI, p_, p_ + 1);
        return true;
      case 44:
        emit(T_COMMA, p_, p_ + 1);
        return true;
      case 42:
        emit(T_MULT, p_, p_ + 1);
        return true;
      case 120:
      case 111:
        return link(c) || node_string();
      case 60:
        if (link(c)) return true;
        emit(T_TAGSTART, p_, p_ + 1);
        return true;
      case 45:
        if (link(c) || node_string()) return true;
        emit(T_MINUS, p_, p_ + 1);
        return true;
      case 61:
        return link(c);
      case 46:
        return link(c) || node_string();
      case 126:
        return invis_link();
      case 40:
        return opener(c);
      case 91:
        if (at(p_ + 1) == 124) {
          emit(T_VERTEX_WITH_PROPS_START, p_, p_ + 2);
          return true;
        }
        return opener(c);
      case 124:
      case 123:
        return opener(c);
      case 62:
        push_state(S_text);
        emit(T_TAGEND, p_, p_ + 1);
        return true;
      case 94:
        emit(T_UP, p_, p_ + 1);
        return true;
      case 92:
        if (at(p_ + 1) == 124) {
          emit(T_SEP, p_, p_ + 2);
          return true;
        }
        return node_string();
      case 118:
        if (!is_word(at(p_ + 1))) {
          emit(T_DOWN, p_, p_ + 1);
          return true;
        }
        return node_string();
      default:
        break;
    }

    if (is_space(c)) {
      if (link(c)) return true;
      std::size_t q = p_;
      if (c == '\r' || c == '\n') {
        // (?:\r?\n)+
        std::size_t k = p_;
        while (true) {
          std::size_t j = k;
          if (at(j) == '\r') j++;
          if (at(j) != '\n') break;
          k = j + 1;
        }
        if (k > p_) {
          emit(T_NEWLINE, p_, k);
          return true;
        }
      }
      // [^\S\n\r]+
      q = p_;
      while (q < n_ && is_space(src_[q]) && src_[q] != '\n' && src_[q] != '\r') q++;
      if (q > p_) {
        emit(T_SPACE, p_, q);
        return true;
      }
      return false;
    }

    if (c < 128) return node_string();
    if (is_unicode_letter(c)) {
      emit(T_UNICODE_TEXT, p_, p_ + 1);
      return true;
    }
    return false;
  }

  bool edge_text(int c, link_kind kind, int a, int b) {
    if (c == 34) {
      quote();
      return true;
    }
    const int d = link_char(p_);
    if (d == a || d == b) {
      const index e = m_link_of(kind, p_);
      if (e >= 0) {
        pop();
        emit(T_LINK, p_, static_cast<std::size_t>(e));
        return true;
      }
    }
    if (c == a && (a != 45 || at(p_ + 1) == 45)) return false;
    std::size_t q = p_ + 1;
    while (q < n_) {
      const int ch = src_[q];
      if (ch == 34) break;
      if (ch == a) {
        if (a != 45 || at(q + 1) == 45) break;
      } else if (ch == 120 || ch == 111 || ch == 60 || ch == b || is_space(ch)) {
        const int dd = link_char(q);
        if ((dd == a || dd == b) && m_link_of(kind, q) >= 0) break;
      }
      q++;
    }
    emit(T_EDGE_TEXT, p_, q);
    return true;
  }

  void lex_string(int c) {
    if (c == 34) {
      if (at(p_ + 1) == 96) {
        push_state(S_md_string);
        p_ += 2;
      } else {
        pop();
        p_++;
      }
    } else {
      std::size_t e = src_.find(u'"', p_);
      emit(T_STR, p_, e == ustring::npos ? n_ : e);
    }
  }

  bool lex_md_string(int c) {
    if (c == 96) {
      if (at(p_ + 1) == 34) {
        pop();
        p_ += 2;
      } else {
        return false;
      }
    } else if (c == 34) {
      quote();
    } else {
      std::size_t q = p_ + 1;
      while (q < n_) {
        const int ch = src_[q];
        if (ch == 96 || ch == 34) break;
        q++;
      }
      emit(T_MD_STR, p_, q);
    }
    return true;
  }

  bool lex_dir(int c) {
    index e;
    if (c == 34) {
      quote();
    } else if ((e = m_nodir(p_)) >= 0) {
      pop();
      emit(T_NODIR, p_, e);
    } else if ((e = m_dir(p_)) >= 0 || (e = m_dir_sym(p_)) >= 0 || (e = m_dir_v(p_)) >= 0) {
      pop();
      emit(T_DIR, p_, e);
    } else {
      return invis_link() || opener(c);
    }
    return true;
  }

  void lex_text(int c) {
    const int d = at(p_ + 1);
    switch (c) {
      case 34: quote(); break;
      case 40:
      case 91:
      case 123: opener(c); break;
      case 93:
        pop();
        if (d == 41) emit(T_STADIUMEND, p_, p_ + 2);
        else if (d == 93) emit(T_SUBROUTINEEND, p_, p_ + 2);
        else emit(T_SQE, p_, p_ + 1);
        break;
      case 41:
        pop();
        if (d == 93) emit(T_CYLINDEREND, p_, p_ + 2);
        else if (d == 41 && at(p_ + 2) == 41) emit(T_DOUBLECIRCLEEND, p_, p_ + 3);
        else emit(T_PE, p_, p_ + 1);
        break;
      case 124:
        pop();
        emit(T_PIPE, p_, p_ + 1);
        break;
      case 125:
        pop();
        emit(T_DIAMOND_STOP, p_, p_ + 1);
        break;
      default: {
        if ((c == 126 || is_space(c)) && invis_link()) break;
        std::size_t q = p_ + 1;
        while (q < n_) {
          const int ch = src_[q];
          if (ch == 91 || ch == 93 || ch == 40 || ch == 41 || ch == 123 || ch == 125 || ch == 124 || ch == 34) break;
          q++;
        }
        emit(T_TEXT, p_, q);
      }
    }
  }

  bool lex_ellipse_text(int c) {
    const int d = at(p_ + 1);
    if (c == 34) {
      quote();
    } else if ((c == 126 || is_space(c)) && invis_link()) {
      return true;
    } else if ((c == 45 || c == 47 || c == 41) && d == 41) {
      pop();
      emit(T_ELLIPSE_END, p_, p_ + 2);
    } else if (c == 40 || c == 91 || c == 123) {
      opener(c);
    } else if (c == 41 || c == 93 || c == 125) {
      return false;
    } else {
      std::size_t q = p_ + 1;
      while (q < n_) {
        const int ch = src_[q];
        if (ch == 34 || ch == 40 || ch == 41 || ch == 91 || ch == 93 || ch == 123 || ch == 125) break;
        if ((ch == 45 || ch == 47) && at(q + 1) == 41) break;
        if ((ch == 126 || is_space(ch)) && invis_ahead(q) && m_invis(q) >= 0) break;
        q++;
      }
      emit(T_TEXT, p_, q);
    }
    return true;
  }

  bool lex_trap_text(int c) {
    const int d = at(p_ + 1);
    if (c == 34) {
      quote();
      return true;
    }
    if ((c == 126 || is_space(c)) && invis_link()) return true;
    switch (c) {
      case 40:
        if (d == 45 || d == 91 || (d == 40 && at(p_ + 2) == 40)) {
          opener(c);
        } else if (d == 93) {
          pop();
          emit(T_TRAPEND, p_, p_ + 2);
        } else {
          opener(c);
        }
        break;
      case 91:
      case 123:
        opener(c);
        break;
      case 92:
        if (d == 93) {
          pop();
          emit(T_TRAPEND, p_, p_ + 2);
        } else {
          emit(T_TEXT, p_, p_ + 1);
        }
        break;
      case 47:
        if (d == 93) {
          pop();
          emit(T_INVTRAPEND, p_, p_ + 2);
        } else {
          emit(T_TEXT, p_, p_ + 1);
        }
        break;
      case 93:
      case 41:
        if (d == 93) {
          pop();
          emit(T_TRAPEND, p_, p_ + 2);
        } else {
          return false;
        }
        break;
      case 125:
        return false;
      default: {
        if ((c == 63 || c == 61) && d == 93) {
          pop();
          emit(T_TRAPEND, p_, p_ + 2);
          break;
        }
        std::size_t q = p_ + 1;
        while (q < n_) {
          const int ch = src_[q];
          if (ch == 92 || ch == 91 || ch == 93 || ch == 40 || ch == 41 || ch == 123 || ch == 125 || ch == 47) break;
          q++;
        }
        emit(T_TEXT, p_, q);
      }
    }
    return true;
  }

  void lex_click(int c) {
    if (c == 34) {
      quote();
    } else if (is_space(c)) {
      pop();
      p_++;
    } else {
      std::size_t q = p_ + 1;
      while (q < n_ && !is_space(src_[q])) q++;
      emit(T_CLICK, p_, q);
    }
  }

  void lex_callbackname(int c) {
    if (c == 40) {
      // \(\s*\)
      const std::size_t k = skip_ws(p_ + 1);
      const index e = at(k) == 41 ? static_cast<index>(k + 1) : -1;
      pop();
      if (e >= 0) {
        p_ = static_cast<std::size_t>(e);
      } else {
        push_state(S_callbackargs);
        p_++;
      }
    } else {
      std::size_t e = src_.find(u'(', p_);
      if (e == ustring::npos) e = n_;
      emit(T_CALLBACKNAME, p_, e);
    }
  }

  bool lex_shape_data(int c) {
    if (c == 34) {
      push_state(S_shapeDataStr);
      emit(T_SHAPE_DATA, p_, p_ + 1);
    } else if (c == 125) {
      pop();
      p_++;
    } else if (c == 94) {
      return false;
    } else {
      std::size_t q = p_ + 1;
      while (q < n_) {
        const int ch = src_[q];
        if (ch == 125 || ch == 94 || ch == 34) break;
        q++;
      }
      emit(T_SHAPE_DATA, p_, q);
    }
    return true;
  }

  void lex_shape_data_str(int c) {
    if (c == 34) {
      pop();
      emit(T_SHAPE_DATA, p_, p_ + 1);
    } else {
      std::size_t e = src_.find(u'"', p_);
      if (e == ustring::npos) e = n_;
      // /\n\s*/g becomes <br/>
      ustring text;
      for (std::size_t i = p_; i < e;) {
        if (src_[i] == '\n') {
          text += u"<br/>";
          i++;
          while (i < e && is_space(src_[i])) i++;
        } else {
          text += src_[i++];
        }
      }
      out_.types.push_back(T_SHAPE_DATA);
      out_.texts.push_back(std::move(text));
      out_.starts.push_back(p_);
      p_ = e;
    }
  }

  const ustring& src_;
  std::size_t n_;
  token_list out_;
  std::vector<int> stack_;
  int state_ = S_INITIAL;
  std::size_t p_ = 0;
  bool first_graph_ = true;
  index dir_next_ = -1;
  index at_next_ = -1;
  index line_end_ = -1;
  index dir_failed_ = 0;
  index run_end_ = 0;
  index run_at_ = -1;
  index ws_from_ = -1;
  index ws_to_ = -1;
};

}  // namespace

token_list tokenize(const ustring& source) { return lexer(source).run(); }

}  // namespace pele::flowchart
