#include "parser.h"

#include <array>

#include "chars.h"
#include "errors.h"
#include "lexer.h"

namespace pele::flowchart {
namespace {

using table = std::array<bool, kTokenCount>;

table make_table(std::initializer_list<int> types) {
  table t{};
  for (int type : types) t[type] = true;
  return t;
}

const table& id_tokens() {
  static const table t = make_table({T_NUM, T_NODE_STRING, T_DOWN, T_MINUS, T_DEFAULT, T_COMMA, T_COLON, T_AMP, T_BRKT, T_MULT, T_UNICODE_TEXT});
  return t;
}

const table& alphanum_tokens() {
  static const table t = make_table({T_NUM, T_UNICODE_TEXT, T_NODE_STRING, T_DIR, T_DOWN, T_MINUS, T_COMMA, T_COLON, T_AMP, T_BRKT, T_MULT});
  return t;
}

const table& no_tags_tokens() {
  static const table t = make_table({T_NUM, T_NODE_STRING, T_SPACE, T_MINUS, T_AMP, T_UNICODE_TEXT, T_COLON, T_MULT, T_BRKT, T_STYLE, T_LINKSTYLE, T_CLASSDEF, T_CLASS,
                                     T_CLICK, T_GRAPH, T_DIR, T_subgraph, T_end_kw, T_DOWN, T_UP, T_START_LINK});
  return t;
}

const table& style_part_tokens() {
  static const table t = make_table({T_NUM, T_NODE_STRING, T_COLON, T_SPACE, T_BRKT, T_STYLE});
  return t;
}

const char* const kTextTokens = "'UNICODE_TEXT', 'TEXT', 'TAGSTART'";
const char* const kStatementStart =
    "'SEMI', 'NEWLINE', 'SPACE', 'EOF', 'subgraph', 'acc_title', 'acc_descr', 'acc_descr_multiline_value', 'STYLE', 'LINKSTYLE', 'CLASSDEF', 'CLASS', 'CLICK', "
    "'NODE_STRING'";
const char* const kDirections[5] = {"TB", "BT", "RL", "LR", "TD"};

// Mermaid trims whitespace between a closing brace and the end of its line before parsing: `}\s*\n` is `}\n`.
ustring trim_brace_trail(const ustring& source) {
  if (source.find(u'}') == ustring::npos) return source;
  ustring out;
  for (std::size_t i = 0; i < source.size();) {
    if (source[i] != '}') {
      out += source[i++];
      continue;
    }
    std::size_t r = i + 1;
    while (r < source.size() && is_space(source[r])) r++;
    std::size_t last_nl = ustring::npos;
    for (std::size_t k = r; k > i + 1; k--)
      if (source[k - 1] == '\n') {
        last_nl = k - 1;
        break;
      }
    if (last_nl != ustring::npos) {
      out += u"}\n";
      i = last_nl + 1;
    } else {
      out += source[i++];
    }
  }
  return out;
}

// An open subgraph: what its header said, and the state of the level around it.
struct frame {
  std::optional<flow_text> id;
  std::optional<flow_text> title;
  bool title_is_id = false;
  std::vector<doc_item> list;
  bool collect = false;
};

class parser {
 public:
  parser(const ustring& source, flow_db& db) : src_(trim_brace_trail(source)), db_(db) {
    token_list tokens = tokenize(src_);
    types_ = std::move(tokens.types);
    texts_ = std::move(tokens.texts);
    starts_ = std::move(tokens.starts);
  }

  void run() {
    while (ty() == T_SPACE || ty() == T_NEWLINE) i_++;
    expect(T_GRAPH, "'SPACE', 'NEWLINE'");
    if (ty() == T_NODIR) {
      i_++;
      db_.set_direction(u"TB");
    } else {
      const ustring dir = expect(T_DIR, "'NODIR'");
      int t = ty();
      if (t == T_SPACE) {
        do i_++;
        while (ty() == T_SPACE);
        t = ty();
        if (t != T_NEWLINE) fail("'NEWLINE', 'SPACE'");
      } else if (t != T_SEMI && t != T_NEWLINE) {
        fail("'SEMI', 'NEWLINE', 'SPACE'");
      }
      i_++;
      db_.set_direction(dir);
    }
    document();
    if (ty() != T_END) fail(kStatementStart);
  }

 private:
  int ty() const { return i_ < types_.size() ? types_[i_] : -2; }

  [[noreturn]] void fail(const std::string& expected) {
    const int t = ty();
    const std::size_t at = i_ < starts_.size() ? starts_[i_] : src_.size();
    if (t == T_ERROR) throw syntax_error("flowchart", src_, at, u"", true);
    const std::string name = t >= 0 && t < kTokenCount ? kTokenNames[t] : "undefined";
    throw syntax_error("flowchart", src_, at, ascii("Expecting " + expected + ", got '" + name + "'"));
  }

  ustring expect(int type, const std::string& also = "") {
    if (ty() != type) fail(std::string("'") + kTokenNames[type] + "'" + (also.empty() ? "" : ", " + also));
    return texts_[i_++];
  }

  void separator() {
    const int t = ty();
    if (t != T_NEWLINE && t != T_SEMI && t != T_EOF) fail("'SEMI', 'NEWLINE', 'EOF'");
    i_++;
  }

  ustring id_string() {
    if (ty() < 0 || !id_tokens()[ty()]) fail("'NODE_STRING'");
    ustring s = texts_[i_++];
    while (ty() >= 0 && id_tokens()[ty()]) s += texts_[i_++];
    return s;
  }

  ustring alpha_num() {
    if (ty() < 0 || !alphanum_tokens()[ty()]) fail("'NODE_STRING'");
    ustring s = texts_[i_++];
    while (ty() >= 0 && alphanum_tokens()[ty()]) s += texts_[i_++];
    return s;
  }

  static bool is_text_token(int n) { return n == T_TEXT || n == T_TAGSTART || n == T_TAGEND || n == T_UNICODE_TEXT; }

  flow_text text() {
    const int t = ty();
    label_type type;
    if (t == T_STR) type = label_type::string;
    else if (t == T_MD_STR) type = label_type::markdown;
    else if (is_text_token(t)) type = label_type::text;
    else fail(std::string(kTextTokens) + ", 'TAGEND', 'STR', 'MD_STR'");
    ustring s = texts_[i_++];
    while (is_text_token(ty())) s += texts_[i_++];
    return {s, type};
  }

  void closed(int type) { expect(type, kTextTokens); }

  flow_text edge_text() {
    const int t = ty();
    label_type type;
    if (t == T_STR) type = label_type::string;
    else if (t == T_MD_STR) type = label_type::markdown;
    else if (t == T_EDGE_TEXT || t == T_UNICODE_TEXT) type = label_type::text;
    else fail("'EDGE_TEXT', 'STR', 'MD_STR'");
    ustring s = texts_[i_++];
    while (ty() == T_EDGE_TEXT || ty() == T_UNICODE_TEXT) s += texts_[i_++];
    return {s, type};
  }

  flow_text text_no_tags() {
    const int t = ty();
    label_type type;
    if (t == T_STR) type = label_type::text;
    else if (t == T_MD_STR) type = label_type::markdown;
    else if (t >= 0 && no_tags_tokens()[t]) type = label_type::text;
    else fail("'NODE_STRING', 'STR', 'MD_STR'");
    ustring s = texts_[i_++];
    while (ty() >= 0 && no_tags_tokens()[ty()]) s += texts_[i_++];
    return {s, type};
  }

  ustring shape_data() {
    ustring s = texts_[i_++];
    while (ty() == T_SHAPE_DATA) s += texts_[i_++];
    return s;
  }

  void add(const ustring& id, const flow_text& label, const char* type) {
    vertex_args a;
    a.text = label;
    a.type = std::string(type);
    db_.add_vertex(id, std::move(a));
  }

  ustring vertex() {
    const ustring id = id_string();
    flow_text label;
    switch (ty()) {
      case T_SQS:
        i_++;
        label = text();
        closed(T_SQE);
        add(id, label, "square");
        break;
      case T_DOUBLECIRCLESTART:
        i_++;
        label = text();
        closed(T_DOUBLECIRCLEEND);
        add(id, label, "doublecircle");
        break;
      case T_PS:
        i_++;
        if (ty() == T_PS) {
          i_++;
          label = text();
          closed(T_PE);
          expect(T_PE);
          add(id, label, "circle");
        } else {
          label = text();
          closed(T_PE);
          add(id, label, "round");
        }
        break;
      case T_ELLIPSE_START:
        i_++;
        label = text();
        closed(T_ELLIPSE_END);
        add(id, label, "ellipse");
        break;
      case T_STADIUMSTART:
        i_++;
        label = text();
        closed(T_STADIUMEND);
        add(id, label, "stadium");
        break;
      case T_SUBROUTINESTART:
        i_++;
        label = text();
        closed(T_SUBROUTINEEND);
        add(id, label, "subroutine");
        break;
      case T_VERTEX_WITH_PROPS_START: {
        i_++;
        const ustring field = expect(T_NODE_STRING);
        expect(T_COLON);
        const ustring value = expect(T_NODE_STRING);
        expect(T_PIPE);
        label = text();
        closed(T_SQE);
        vertex_args a;
        a.text = label;
        a.type = std::string("rect");
        a.props.emplace_back(field, value);
        a.has_props = true;
        db_.add_vertex(id, std::move(a));
        break;
      }
      case T_CYLINDERSTART:
        i_++;
        label = text();
        closed(T_CYLINDEREND);
        add(id, label, "cylinder");
        break;
      case T_DIAMOND_START:
        i_++;
        if (ty() == T_DIAMOND_START) {
          i_++;
          label = text();
          closed(T_DIAMOND_STOP);
          expect(T_DIAMOND_STOP);
          add(id, label, "hexagon");
        } else {
          label = text();
          closed(T_DIAMOND_STOP);
          add(id, label, "diamond");
        }
        break;
      case T_TAGEND:
        i_++;
        label = text();
        closed(T_SQE);
        add(id, label, "odd");
        break;
      case T_TRAPSTART:
        i_++;
        label = text();
        if (ty() == T_INVTRAPEND) {
          i_++;
          add(id, label, "lean_right");
        } else {
          closed(T_TRAPEND);
          add(id, label, "trapezoid");
        }
        break;
      case T_INVTRAPSTART:
        i_++;
        label = text();
        if (ty() == T_TRAPEND) {
          i_++;
          add(id, label, "lean_left");
        } else {
          closed(T_INVTRAPEND);
          add(id, label, "inv_trapezoid");
        }
        break;
      default:
        db_.add_vertex(id);
    }
    return id;
  }

  ustring styled_vertex() {
    const ustring id = vertex();
    if (ty() == T_STYLE_SEPARATOR) {
      i_++;
      db_.set_class(id, id_string());
    }
    return id;
  }

  void add_data(const ustring& id, const ustring& data) {
    vertex_args a;
    a.metadata = data;
    db_.add_vertex(id, std::move(a));
  }

  std::vector<ustring> node() {
    std::vector<ustring> ids = {styled_vertex()};
    pending_data_.reset();
    while (true) {
      std::optional<ustring> data;
      if (ty() == T_SHAPE_DATA) data = shape_data();
      if (ty() != T_SPACE) {
        pending_data_ = data;
        return ids;
      }
      do i_++;
      while (ty() == T_SPACE);
      if (ty() != T_AMP) {
        if (data) fail("'AMP'");
        return ids;
      }
      i_++;
      if (ty() != T_SPACE) fail("'SPACE'");
      do i_++;
      while (ty() == T_SPACE);
      const ustring next = styled_vertex();
      if (data) add_data(ids.back(), *data);
      ids.push_back(next);
    }
  }

  link_info link() {
    std::optional<ustring> id;
    if (ty() == T_LINK_ID) id = texts_[i_++];
    if (ty() == T_LINK) {
      link_info info = flow_db::destruct_link(texts_[i_++]);
      if (id) info.id = id;
      if (ty() == T_PIPE) {
        i_++;
        info.text = text();
        closed(T_PIPE);
        if (ty() == T_SPACE) i_++;
      }
      return info;
    }
    if (ty() == T_START_LINK) {
      const ustring start = texts_[i_++];
      const flow_text label = edge_text();
      const ustring end = expect(T_LINK, "'EDGE_TEXT'");
      link_info info = flow_db::destruct_link(end, start);
      info.text = label;
      if (id) info.id = id;
      return info;
    }
    fail("'LINK', 'START_LINK'");
  }

  std::vector<ustring> vertex_statement() {
    std::vector<ustring> stmt = node();
    if (pending_data_) add_data(stmt.back(), *pending_data_);
    std::vector<ustring> nodes = stmt;
    while (true) {
      const int t = ty();
      if (t != T_LINK && t != T_START_LINK && t != T_LINK_ID) return nodes;
      const link_info info = link();
      std::vector<ustring> next = node();
      if (pending_data_) add_data(next.back(), *pending_data_);
      db_.add_link(stmt, next, info);
      std::vector<ustring> merged = next;
      merged.insert(merged.end(), nodes.begin(), nodes.end());
      nodes = std::move(merged);
      stmt = std::move(next);
    }
  }

  ustring style() {
    if (ty() < 0 || !style_part_tokens()[ty()]) fail("'NODE_STRING', 'NUM', 'COLON', 'BRKT'");
    ustring s = texts_[i_++];
    while (ty() >= 0 && style_part_tokens()[ty()]) s += texts_[i_++];
    return s;
  }

  std::vector<ustring> styles_opt() {
    std::vector<ustring> out = {style()};
    while (ty() == T_COMMA) {
      i_++;
      out.push_back(style());
    }
    return out;
  }

  void link_style_statement() {
    i_++;
    expect(T_SPACE);
    std::vector<ustring> positions;
    if (ty() == T_DEFAULT) {
      positions = {texts_[i_++]};
    } else {
      positions = {expect(T_NUM, "'DEFAULT'")};
      while (ty() == T_COMMA) {
        i_++;
        positions.push_back(expect(T_NUM));
      }
    }
    expect(T_SPACE);
    if (ty() != T_INTERPOLATE) {
      db_.update_link(positions, styles_opt());
      return;
    }
    i_++;
    expect(T_SPACE);
    const std::string interpolate = to_utf8(alpha_num());
    if (ty() == T_SPACE) {
      i_++;
      std::vector<ustring> styles = styles_opt();
      db_.update_link_interpolate(positions, interpolate);
      db_.update_link(positions, std::move(styles));
    } else {
      db_.update_link_interpolate(positions, interpolate);
    }
  }

  void link_tail(const ustring& id, const ustring& url) {
    if (ty() != T_SPACE) {
      db_.set_link(id, url);
      return;
    }
    i_++;
    if (ty() == T_LINK_TARGET) {
      db_.set_link(id, url, texts_[i_++]);
      return;
    }
    const ustring tooltip = expect(T_STR, "'LINK_TARGET'");
    if (ty() == T_SPACE) {
      i_++;
      db_.set_link(id, url, expect(T_LINK_TARGET));
    } else {
      db_.set_link(id, url);
    }
    db_.set_tooltip(id, tooltip);
  }

  void click_statement() {
    const ustring id = texts_[i_++];
    switch (ty()) {
      case T_CALLBACKNAME: {
        i_++;
        if (ty() == T_CALLBACKARGS) i_++;
        std::optional<ustring> tooltip;
        if (ty() == T_SPACE) {
          i_++;
          tooltip = expect(T_STR);
        }
        db_.set_click_event(id);
        if (tooltip) db_.set_tooltip(id, tooltip);
        return;
      }
      case T_HREF:
        i_++;
        link_tail(id, expect(T_STR));
        return;
      case T_STR:
        link_tail(id, texts_[i_++]);
        return;
      default: {
        alpha_num();
        if (ty() == T_SPACE) {
          i_++;
          const ustring tooltip = expect(T_STR);
          db_.set_click_event(id);
          db_.set_tooltip(id, tooltip);
        } else {
          db_.set_click_event(id);
        }
      }
    }
  }

  frame subgraph_header() {
    i_++;
    int t = ty();
    if (t == T_NEWLINE || t == T_SEMI || t == T_EOF) {
      i_++;
      return frame{};
    }
    expect(T_SPACE, "'SEMI', 'NEWLINE', 'EOF'");
    frame f;
    f.id = text_no_tags();
    f.title = f.id;
    f.title_is_id = true;
    if (ty() == T_SQS) {
      i_++;
      f.title = text();
      f.title_is_id = false;
      closed(T_SQE);
    }
    t = ty();
    if (t != T_NEWLINE && t != T_SEMI && t != T_EOF) fail("'SEMI', 'NEWLINE', 'EOF', 'SQS'");
    i_++;
    return f;
  }

  // A statement gives an item of the subgraph that holds it; an empty list means it gives none.
  std::vector<doc_item> statement() {
    const int t = ty();
    switch (t) {
      case T_STYLE: {
        i_++;
        expect(T_SPACE);
        const ustring id = id_string();
        expect(T_SPACE);
        vertex_args a;
        a.style = styles_opt();
        db_.add_vertex(id, std::move(a));
        separator();
        return {};
      }
      case T_LINKSTYLE:
        link_style_statement();
        separator();
        return {};
      case T_CLASSDEF: {
        i_++;
        expect(T_SPACE);
        const ustring id = id_string();
        expect(T_SPACE);
        db_.add_class(id, styles_opt());
        separator();
        return {};
      }
      case T_CLASS: {
        i_++;
        expect(T_SPACE);
        const ustring ids = id_string();
        expect(T_SPACE);
        db_.set_class(ids, id_string());
        separator();
        return {};
      }
      case T_CLICK:
        click_statement();
        separator();
        return {};
      case T_direction_tb:
      case T_direction_bt:
      case T_direction_rl:
      case T_direction_lr:
      case T_direction_td: {
        i_++;
        doc_item item;
        item.what = doc_item::kind::direction;
        item.direction = kDirections[t - T_direction_tb];
        return {item};
      }
      case T_acc_title: {
        i_++;
        const ustring value = trim(expect(T_acc_title_value));
        db_.set_acc_title(value);
        return {name_item(value)};
      }
      case T_acc_descr: {
        i_++;
        const ustring value = trim(expect(T_acc_descr_value));
        db_.set_acc_description(value);
        return {name_item(value)};
      }
      case T_acc_descr_multiline_value: {
        const ustring value = trim(texts_[i_++]);
        db_.set_acc_description(value);
        return {name_item(value)};
      }
      default: {
        if (t < 0 || !id_tokens()[t]) fail(kStatementStart);
        doc_item item;
        item.what = doc_item::kind::names;
        item.names = vertex_statement();
        separator();
        return {item};
      }
    }
  }

  static doc_item name_item(const ustring& s) {
    doc_item item;
    item.what = doc_item::kind::name;
    item.name = s;
    return item;
  }

  // Subgraphs nest without recursion: each open one keeps the list and collect flag of its parent.
  void document() {
    std::vector<frame> open;
    std::vector<doc_item> list;
    bool collect = false;
    while (true) {
      const int t = ty();
      if (t == T_END) break;
      if (t == T_end_kw) {
        if (open.empty()) break;
        frame f = std::move(open.back());
        open.pop_back();
        i_++;
        const ustring id = db_.add_sub_graph(f.id, list, f.title, f.title_is_id);
        list = std::move(f.list);
        collect = f.collect;
        if (collect) list.push_back(name_item(id));
      } else if (t == T_SEMI || t == T_NEWLINE || t == T_SPACE || t == T_EOF) {
        if (collect) list.push_back(name_item(texts_[i_]));
        i_++;
      } else if (t == T_subgraph) {
        frame f = subgraph_header();
        f.list = std::move(list);
        f.collect = collect;
        open.push_back(std::move(f));
        list.clear();
        collect = true;
      } else {
        std::vector<doc_item> items = statement();
        if (collect)
          for (doc_item& item : items) list.push_back(std::move(item));
      }
    }
    if (!open.empty()) fail("'end'");
  }

  ustring src_;
  flow_db& db_;
  std::vector<int> types_;
  std::vector<ustring> texts_;
  std::vector<std::size_t> starts_;
  std::size_t i_ = 0;
  std::optional<ustring> pending_data_;
};

}  // namespace

void parse_flowchart(const ustring& source, flow_db& db) { parser(source, db).run(); }

}  // namespace pele::flowchart
