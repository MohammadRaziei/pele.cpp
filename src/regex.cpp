#include "regex.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "chars.h"

namespace pele {
namespace {

// ---- character classes ----

enum class cls : std::uint8_t { digit, not_digit, word, not_word, space, not_space };

struct char_set {
  std::vector<std::pair<char16_t, char16_t>> ranges;
  std::vector<cls> escapes;
  bool negate = false;
};

char16_t canonical(char16_t c) {
  if (c < 128) return c >= 'a' && c <= 'z' ? static_cast<char16_t>(c - 32) : c;
  if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return static_cast<char16_t>(c - 0x20);
  if (c == 0xFF) return 0x178;
  if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return static_cast<char16_t>(c - 0x20);
  if (c == 0x3C2) return 0x3A3;
  if (c >= 0x430 && c <= 0x44F) return static_cast<char16_t>(c - 0x20);
  if (c >= 0x450 && c <= 0x45F) return static_cast<char16_t>(c - 0x50);
  return c;
}

bool escape_matches(cls e, char16_t c) {
  switch (e) {
    case cls::digit: return is_digit(c);
    case cls::not_digit: return !is_digit(c);
    case cls::word: return is_word(c);
    case cls::not_word: return !is_word(c);
    case cls::space: return is_space(c);
    default: return !is_space(c);
  }
}

bool set_matches(const char_set& set, char16_t c, bool ignore_case) {
  bool in = false;
  for (const auto& r : set.ranges) {
    if (c >= r.first && c <= r.second) {
      in = true;
      break;
    }
  }
  if (!in && ignore_case) {
    const char16_t cc = canonical(c);
    for (const auto& r : set.ranges) {
      // A range matches if any character in it has the same canonical form.
      if (r.second - r.first > 512) {
        if (cc >= r.first && cc <= r.second) in = true;
      } else {
        for (unsigned k = r.first; k <= r.second && !in; k++)
          if (canonical(static_cast<char16_t>(k)) == cc) in = true;
      }
      if (in) break;
    }
  }
  if (!in)
    for (cls e : set.escapes)
      if (escape_matches(e, c)) {
        in = true;
        break;
      }
  return in != set.negate;
}

// ---- the syntax tree ----

struct node;
using node_ptr = std::unique_ptr<node>;

enum class kind { empty, ch, any, set, seq, alt, repeat, group, look, bol, eol, wordb, nwordb };

struct node {
  kind k = kind::empty;
  char16_t c = 0;
  char_set set;
  std::vector<node_ptr> kids;
  int min = 0;
  int max = -1;  // -1: no limit
  bool greedy = true;
  int group = -1;  // capture index, or -1 for a group that does not capture
  bool negative = false;
};

class parser {
 public:
  parser(const ustring& src, bool ignore_case) : s_(src), ignore_case_(ignore_case) {}

  node_ptr parse(int& groups) {
    node_ptr n = disjunction();
    if (i_ < s_.size()) fail("Unmatched ')'");
    groups = groups_;
    return n;
  }

 private:
  [[noreturn]] void fail(const std::string& what) const { throw std::runtime_error("Invalid regular expression: " + what); }

  bool more() const { return i_ < s_.size(); }
  int peek() const { return i_ < s_.size() ? s_[i_] : -1; }

  node_ptr make(kind k) {
    auto n = std::make_unique<node>();
    n->k = k;
    return n;
  }

  node_ptr disjunction() {
    node_ptr first = alternative();
    if (peek() != '|') return first;
    node_ptr alt = make(kind::alt);
    alt->kids.push_back(std::move(first));
    while (peek() == '|') {
      i_++;
      alt->kids.push_back(alternative());
    }
    return alt;
  }

  node_ptr alternative() {
    node_ptr seq = make(kind::seq);
    while (more() && peek() != '|' && peek() != ')') seq->kids.push_back(term());
    return seq;
  }

  // `{n}`, `{n,}` or `{n,m}` at i, or false.
  bool quantifier_braces(int& min, int& max) {
    std::size_t k = i_ + 1;
    auto number = [&](int& out) {
      std::size_t from = k;
      long v = 0;
      while (k < s_.size() && is_digit(s_[k])) {
        v = v * 10 + (s_[k] - '0');
        if (v > 100000) v = 100000;
        k++;
      }
      out = static_cast<int>(v);
      return k > from;
    };
    if (!number(min)) return false;
    max = min;
    if (k < s_.size() && s_[k] == ',') {
      k++;
      if (k < s_.size() && s_[k] == '}') max = -1;
      else if (!number(max)) return false;
    }
    if (k >= s_.size() || s_[k] != '}') return false;
    i_ = k + 1;
    return true;
  }

  node_ptr term() {
    const int c = peek();
    node_ptr atom;
    if (c == '^') {
      i_++;
      return make(kind::bol);
    }
    if (c == '$') {
      i_++;
      return make(kind::eol);
    }
    if (c == '\\' && (i_ + 1 < s_.size()) && (s_[i_ + 1] == 'b' || s_[i_ + 1] == 'B')) {
      const bool b = s_[i_ + 1] == 'b';
      i_ += 2;
      return make(b ? kind::wordb : kind::nwordb);
    }
    if (c == '(' && i_ + 2 < s_.size() && s_[i_ + 1] == '?' && (s_[i_ + 2] == '=' || s_[i_ + 2] == '!')) {
      const bool negative = s_[i_ + 2] == '!';
      i_ += 3;
      node_ptr look = make(kind::look);
      look->negative = negative;
      look->kids.push_back(disjunction());
      if (peek() != ')') fail("Unterminated group");
      i_++;
      // A lookahead may be followed by a quantifier in Annex B; it is not used here.
      return look;
    }
    atom = atom_();
    return quantified(std::move(atom));
  }

  node_ptr quantified(node_ptr atom) {
    int min = -1;
    int max = -1;
    const int c = peek();
    if (c == '*') {
      min = 0;
      max = -1;
      i_++;
    } else if (c == '+') {
      min = 1;
      max = -1;
      i_++;
    } else if (c == '?') {
      min = 0;
      max = 1;
      i_++;
    } else if (c == '{') {
      if (!quantifier_braces(min, max)) return atom;
    } else {
      return atom;
    }
    node_ptr rep = make(kind::repeat);
    rep->min = min;
    rep->max = max;
    if (peek() == '?') {
      rep->greedy = false;
      i_++;
    }
    rep->kids.push_back(std::move(atom));
    return rep;
  }

  node_ptr literal(char16_t c) {
    node_ptr n = make(kind::ch);
    n->c = c;
    return n;
  }

  int hex_digits(int count) {
    int v = 0;
    for (int k = 0; k < count; k++) {
      if (!is_hex(peek())) return -1;
      const int d = s_[i_] <= '9' ? s_[i_] - '0' : (s_[i_] | 0x20) - 'a' + 10;
      v = v * 16 + d;
      i_++;
    }
    return v;
  }

  // An escape that stands for one character, after the backslash. Returns -1 where it is a class escape.
  int char_escape() {
    const int e = peek();
    i_++;
    switch (e) {
      case 't': return '\t';
      case 'n': return '\n';
      case 'r': return '\r';
      case 'v': return '\v';
      case 'f': return '\f';
      case '0': return 0;
      case 'x': {
        const std::size_t save = i_;
        const int v = hex_digits(2);
        if (v >= 0) return v;
        i_ = save;
        return 'x';
      }
      case 'u': {
        const std::size_t save = i_;
        const int v = hex_digits(4);
        if (v >= 0) return v;
        i_ = save;
        return 'u';
      }
      case 'c': {
        if (is_letter(peek())) {
          const int v = s_[i_] % 32;
          i_++;
          return v;
        }
        i_--;
        return '\\';
      }
      default: return e;
    }
  }

  bool class_escape(int e, std::vector<cls>& out) {
    switch (e) {
      case 'd': out.push_back(cls::digit); return true;
      case 'D': out.push_back(cls::not_digit); return true;
      case 'w': out.push_back(cls::word); return true;
      case 'W': out.push_back(cls::not_word); return true;
      case 's': out.push_back(cls::space); return true;
      case 'S': out.push_back(cls::not_space); return true;
      default: return false;
    }
  }

  node_ptr atom_() {
    const int c = peek();
    if (c == '(') {
      i_++;
      int group = -1;
      if (peek() == '?') {
        if (i_ + 1 < s_.size() && s_[i_ + 1] == ':') i_ += 2;
        else fail("Invalid group");
      } else {
        group = ++groups_;
      }
      node_ptr g = make(kind::group);
      g->group = group;
      g->kids.push_back(disjunction());
      if (peek() != ')') fail("Unterminated group");
      i_++;
      return g;
    }
    if (c == '[') return bracket();
    if (c == '.') {
      i_++;
      return make(kind::any);
    }
    if (c == '\\') {
      i_++;
      if (!more()) fail("\\ at end of pattern");
      const int e = peek();
      std::vector<cls> escapes;
      if (class_escape(e, escapes)) {
        i_++;
        node_ptr n = make(kind::set);
        n->set.escapes = escapes;
        return n;
      }
      if (e >= '1' && e <= '9') fail("backreferences are not supported");
      return literal(static_cast<char16_t>(char_escape()));
    }
    if (c == '*' || c == '+' || c == '?') fail("Nothing to repeat");
    i_++;
    return literal(static_cast<char16_t>(c));
  }

  node_ptr bracket() {
    i_++;
    node_ptr n = make(kind::set);
    if (peek() == '^') {
      n->set.negate = true;
      i_++;
    }
    // Reads one class atom: a character, or an escape class (then `lo` is -1).
    auto atom = [&](int& lo) {
      const int c = peek();
      i_++;
      if (c != '\\') {
        lo = c;
        return;
      }
      if (!more()) fail("\\ at end of pattern");
      const int e = peek();
      if (class_escape(e, n->set.escapes)) {
        i_++;
        lo = -1;
        return;
      }
      if (e == 'b') {
        i_++;
        lo = '\b';
        return;
      }
      if (e == '-') {
        i_++;
        lo = '-';
        return;
      }
      lo = char_escape();
    };
    while (true) {
      if (!more()) fail("Unterminated character class");
      if (peek() == ']') {
        i_++;
        break;
      }
      int lo;
      atom(lo);
      if (lo < 0) continue;
      if (peek() == '-' && i_ + 1 < s_.size() && s_[i_ + 1] != ']') {
        const std::size_t save = i_;
        i_++;
        int hi;
        atom(hi);
        if (hi < 0) {
          // `a-\d` is the three: a, -, and the class.
          n->set.ranges.emplace_back(static_cast<char16_t>(lo), static_cast<char16_t>(lo));
          n->set.ranges.emplace_back(u'-', u'-');
          continue;
        }
        if (hi < lo) {
          i_ = save;
          fail("Range out of order in character class");
        }
        n->set.ranges.emplace_back(static_cast<char16_t>(lo), static_cast<char16_t>(hi));
      } else {
        n->set.ranges.emplace_back(static_cast<char16_t>(lo), static_cast<char16_t>(lo));
      }
    }
    return n;
  }

  const ustring& s_;
  bool ignore_case_;
  std::size_t i_ = 0;
  int groups_ = 0;
};

// ---- the program ----

enum class op : std::uint8_t { ch, any, set, split, jmp, save, bol, eol, wordb, nwordb, look, match, mark, check, reset };

struct instruction {
  op code = op::match;
  char16_t c = 0;
  int x = 0;  // jump target, group slot or set index
  int y = 0;  // second target for split
  bool flag = false;
};

}  // namespace

struct js_regex::program {
  std::vector<instruction> code;
  std::vector<char_set> sets;
  // Sub-programs of lookaheads, each ending in `match`.
  std::vector<std::vector<instruction>> looks;
  int groups = 0;
  int slots = 0;  // captures (2 per group, group 0 included) and marks
  bool ignore_case = false;
  bool multiline = false;
  bool dot_all = false;
};

namespace {

bool can_be_empty(const node& n) {
  switch (n.k) {
    case kind::ch:
    case kind::any:
    case kind::set: return false;
    case kind::seq:
      for (const auto& k : n.kids)
        if (!can_be_empty(*k)) return false;
      return true;
    case kind::alt:
      for (const auto& k : n.kids)
        if (can_be_empty(*k)) return true;
      return false;
    case kind::repeat: return n.min == 0 || can_be_empty(*n.kids[0]);
    case kind::group: return can_be_empty(*n.kids[0]);
    default: return true;
  }
}

// The lowest and highest capture index inside a node, or -1 for none.
void group_range(const node& n, int& lo, int& hi) {
  if (n.k == kind::group && n.group >= 0) {
    if (lo < 0 || n.group < lo) lo = n.group;
    if (n.group > hi) hi = n.group;
  }
  for (const auto& k : n.kids) group_range(*k, lo, hi);
}

class compiler {
 public:
  compiler(js_regex::program& p) : p_(p) {}

  void compile(const node& root) {
    emit({op::save, 0, 0});
    gen(root, p_.code);
    emit({op::save, 0, 1});
    emit({op::match});
  }

 private:
  void emit(instruction i) { p_.code.push_back(i); }

  int new_mark() { return p_.slots++; }

  // Appends the code for `n` to `out`. Jump targets are absolute within `out`, so a body that is
  // repeated is generated again at its new place.
  void gen(const node& n, std::vector<instruction>& out) {
    auto add = [&](instruction i) {
      out.push_back(i);
      return static_cast<int>(out.size()) - 1;
    };
    switch (n.k) {
      case kind::empty: return;
      case kind::ch: {
        instruction i{op::ch};
        i.c = p_.ignore_case ? canonical(n.c) : n.c;
        add(i);
        return;
      }
      case kind::any: add({op::any}); return;
      case kind::set: {
        p_.sets.push_back(n.set);
        instruction i{op::set};
        i.x = static_cast<int>(p_.sets.size()) - 1;
        add(i);
        return;
      }
      case kind::bol: add({op::bol}); return;
      case kind::eol: add({op::eol}); return;
      case kind::wordb: add({op::wordb}); return;
      case kind::nwordb: add({op::nwordb}); return;
      case kind::seq:
        for (const auto& k : n.kids) gen(*k, out);
        return;
      case kind::alt: {
        std::vector<int> jumps;
        for (std::size_t k = 0; k < n.kids.size(); k++) {
          if (k + 1 < n.kids.size()) {
            const int split = add({op::split});
            out[split].x = static_cast<int>(out.size());
            gen(*n.kids[k], out);
            jumps.push_back(add({op::jmp}));
            out[split].y = static_cast<int>(out.size());
          } else {
            gen(*n.kids[k], out);
          }
        }
        for (int j : jumps) out[j].x = static_cast<int>(out.size());
        return;
      }
      case kind::group: {
        if (n.group >= 0) add({op::save, 0, n.group * 2});
        gen(*n.kids[0], out);
        if (n.group >= 0) add({op::save, 0, n.group * 2 + 1});
        return;
      }
      case kind::look: {
        std::vector<instruction> sub;
        const std::size_t saved = 0;
        (void)saved;
        gen(*n.kids[0], sub);
        sub.push_back({op::match});
        p_.looks.push_back(std::move(sub));
        instruction i{op::look};
        i.x = static_cast<int>(p_.looks.size()) - 1;
        i.flag = n.negative;
        add(i);
        return;
      }
      case kind::repeat: repeat(n, out); return;
    }
  }

  void repeat(const node& n, std::vector<instruction>& out) {
    auto add = [&](instruction i) {
      out.push_back(i);
      return static_cast<int>(out.size()) - 1;
    };
    const node& body = *n.kids[0];
    // Each iteration starts with the captures inside the body unset, as JavaScript has it.
    int lo = -1;
    int hi = -1;
    group_range(body, lo, hi);
    auto start_iteration = [&]() {
      if (lo >= 0) {
        instruction r{op::reset};
        r.x = lo * 2;
        r.y = hi * 2 + 1;
        out.push_back(r);
      }
    };
    for (int k = 0; k < n.min; k++) {
      start_iteration();
      gen(body, out);
    }
    if (n.max == -1) {
      // body*: L1: split L2, L3; L2: body; jmp L1; L3:
      const bool guard = can_be_empty(body);
      const int mark = guard ? new_mark() : -1;
      const int l1 = add({op::split});
      const int l2 = static_cast<int>(out.size());
      if (guard) add({op::mark, 0, mark});
      start_iteration();
      gen(body, out);
      if (guard) add({op::check, 0, mark});
      const int jmp = add({op::jmp});
      out[jmp].x = l1;
      const int l3 = static_cast<int>(out.size());
      out[l1].x = n.greedy ? l2 : l3;
      out[l1].y = n.greedy ? l3 : l2;
      return;
    }
    // (body(body(...)?)?)? for the rest
    std::vector<int> splits;
    for (int k = n.min; k < n.max; k++) {
      const int split = add({op::split});
      splits.push_back(split);
      out[split].x = static_cast<int>(out.size());
      start_iteration();
      // An iteration past the minimum that matches nothing is not allowed.
      const bool guard = can_be_empty(body);
      const int mark = guard ? new_mark() : -1;
      if (guard) add({op::mark, 0, mark});
      gen(body, out);
      if (guard) add({op::check, 0, mark});
    }
    const int end = static_cast<int>(out.size());
    for (int split : splits) {
      const int enter = out[split].x;
      out[split].x = n.greedy ? enter : end;
      out[split].y = n.greedy ? end : enter;
    }
  }

  js_regex::program& p_;
};

// ---- the machine ----

constexpr std::size_t kStepLimit = 200000000;

class machine {
 public:
  machine(const js_regex::program& p, ustring_view s) : p_(p), s_(s) {}

  // Runs the program from `start` at `pos`. On success `caps` has the capture positions (-1 for none).
  bool run(const std::vector<instruction>& code, std::size_t pos, std::vector<long>& caps, std::size_t& end_pos) {
    struct frame {
      int pc;
      std::size_t pos;
      int slot;  // for an undo frame: the slot to restore; else -1
      long old;
    };
    std::vector<frame> stack;
    int pc = 0;
    while (true) {
      if (++steps_ > kStepLimit) throw std::runtime_error("Regular expression is too complex");
      bool ok = true;
      const instruction& in = code[pc];
      switch (in.code) {
        case op::ch:
          if (pos < s_.size() && (p_.ignore_case ? canonical(s_[pos]) : s_[pos]) == in.c) {
            pos++;
            pc++;
          } else {
            ok = false;
          }
          break;
        case op::any:
          if (pos < s_.size() && (p_.dot_all || !is_line_end(s_[pos]))) {
            pos++;
            pc++;
          } else {
            ok = false;
          }
          break;
        case op::set:
          if (pos < s_.size() && set_matches(p_.sets[in.x], s_[pos], p_.ignore_case)) {
            pos++;
            pc++;
          } else {
            ok = false;
          }
          break;
        case op::split:
          stack.push_back({in.y, pos, -1, 0});
          pc = in.x;
          break;
        case op::jmp: pc = in.x; break;
        case op::save:
        case op::mark:
          stack.push_back({0, 0, in.x, caps[in.x]});
          caps[in.x] = static_cast<long>(pos);
          pc++;
          break;
        case op::reset:
          for (int k = in.x; k <= in.y; k++) {
            if (caps[k] != -1) stack.push_back({0, 0, k, caps[k]});
            caps[k] = -1;
          }
          pc++;
          break;
        case op::check:
          if (static_cast<std::size_t>(caps[in.x]) == pos) ok = false;
          else pc++;
          break;
        case op::bol:
          if (pos == 0 || (p_.multiline && is_line_end(s_[pos - 1]))) pc++;
          else ok = false;
          break;
        case op::eol:
          if (pos == s_.size() || (p_.multiline && is_line_end(s_[pos]))) pc++;
          else ok = false;
          break;
        case op::wordb:
        case op::nwordb: {
          const bool a = pos > 0 && is_word(s_[pos - 1]);
          const bool b = pos < s_.size() && is_word(s_[pos]);
          if ((a != b) == (in.code == op::wordb)) pc++;
          else ok = false;
          break;
        }
        case op::look: {
          std::vector<long> inner = caps;
          std::size_t dummy = 0;
          const bool found = run(p_.looks[in.x], pos, inner, dummy);
          if (found != in.flag) {
            if (found)
              for (std::size_t k = 0; k < caps.size(); k++)
                if (caps[k] != inner[k]) {
                  stack.push_back({0, 0, static_cast<int>(k), caps[k]});
                  caps[k] = inner[k];
                }
            pc++;
          } else {
            ok = false;
          }
          break;
        }
        case op::match: end_pos = pos; return true;
      }
      if (ok) continue;
      // Backtrack: undo captures until a choice point.
      bool resumed = false;
      while (!stack.empty()) {
        frame f = stack.back();
        stack.pop_back();
        if (f.slot >= 0) {
          caps[f.slot] = f.old;
        } else {
          pc = f.pc;
          pos = f.pos;
          resumed = true;
          break;
        }
      }
      if (!resumed) return false;
    }
  }

 private:
  const js_regex::program& p_;
  ustring_view s_;
  std::size_t steps_ = 0;
};

}  // namespace

js_regex::js_regex(std::string_view pattern, std::string_view flags) : program_(std::make_unique<program>()) {
  program_->ignore_case = flags.find('i') != std::string_view::npos;
  program_->multiline = flags.find('m') != std::string_view::npos;
  program_->dot_all = flags.find('s') != std::string_view::npos;
  const ustring source = to_utf16(pattern);
  int groups = 0;
  node_ptr root = parser(source, program_->ignore_case).parse(groups);
  program_->groups = groups;
  program_->slots = (groups + 1) * 2;
  compiler(*program_).compile(*root);
}

js_regex::~js_regex() = default;
js_regex::js_regex(js_regex&&) noexcept = default;
js_regex& js_regex::operator=(js_regex&&) noexcept = default;

std::size_t js_regex::group_count() const { return static_cast<std::size_t>(program_->groups); }

std::optional<js_regex::match> js_regex::match_at(ustring_view s, std::size_t at) const {
  if (at > s.size()) return std::nullopt;
  std::vector<long> caps(static_cast<std::size_t>(program_->slots), -1);
  std::size_t end = 0;
  machine m(*program_, s);
  if (!m.run(program_->code, at, caps, end)) return std::nullopt;
  match result;
  result.index = at;
  result.end = end;
  result.groups.resize(static_cast<std::size_t>(program_->groups) + 1);
  for (int g = 0; g <= program_->groups; g++) {
    const long a = caps[static_cast<std::size_t>(g) * 2];
    const long b = caps[static_cast<std::size_t>(g) * 2 + 1];
    if (a >= 0 && b >= 0) result.groups[static_cast<std::size_t>(g)] = std::make_pair(static_cast<std::size_t>(a), static_cast<std::size_t>(b));
  }
  return result;
}

std::optional<js_regex::match> js_regex::search(ustring_view s, std::size_t from) const {
  for (std::size_t at = from; at <= s.size(); at++)
    if (auto m = match_at(s, at)) return m;
  return std::nullopt;
}

std::vector<js_regex::match> js_regex::match_all(ustring_view s) const {
  std::vector<match> out;
  std::size_t from = 0;
  while (from <= s.size()) {
    auto m = search(s, from);
    if (!m) break;
    from = m->end == m->index ? m->end + 1 : m->end;
    out.push_back(std::move(*m));
  }
  return out;
}

ustring js_regex::replace_all(ustring_view s, const std::function<ustring(const match&)>& fn) const {
  ustring out;
  std::size_t last = 0;
  for (const match& m : match_all(s)) {
    out += ustring(s.substr(last, m.index - last));
    out += fn(m);
    last = m.end;
  }
  out += ustring(s.substr(last));
  return out;
}

ustring js_regex::replace_all(ustring_view s, ustring_view replacement) const {
  return replace_all(s, [&](const match&) { return ustring(replacement); });
}

ustring js_regex::replace_first(ustring_view s, const std::function<ustring(const match&)>& fn) const {
  const auto m = search(s, 0);
  if (!m) return ustring(s);
  return ustring(s.substr(0, m->index)) + fn(*m) + ustring(s.substr(m->end));
}

}  // namespace pele
