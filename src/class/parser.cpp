#include "class/parser.h"

#include <algorithm>

#include "class/lexer.h"
#include "class/tokens.h"
#include "errors.h"

namespace pele::classdiagram {
namespace {

constexpr int kCount = T_PUNCTUATION + 1;

bool alphanum(int t) { return t == T_UNICODE_TEXT || t == T_ALPHA || t == T_MINUS; }

bool style_part(int t) { return t == T_ALPHA || t == T_COLON || t == T_BRKT || t == T_STYLE || t == T_PCT || t == T_LABEL; }

// Mermaid's relationType numbers, plus one so that zero means "not an end marker".
int end_type(int t) {
  switch (t) {
    case T_AGGREGATION: return 1;
    case T_EXTENSION: return 2;
    case T_COMPOSITION: return 3;
    case T_DEPENDENCY: return 4;
    case T_LOLLIPOP: return 5;
  }
  return 0;
}

const char* const kNameTokens = "'UNICODE_TEXT', 'NUM', 'ALPHA', 'MINUS', 'BQUOTE_STR'";

struct frame {
  ustring id;
  std::vector<ustring> classes;
  std::vector<ustring> notes;
};

}  // namespace

void parse_class_diagram(const ustring& src, class_db& db) {
  const tokens tk = tokenize(src);
  const std::vector<int>& types = tk.types;
  const std::vector<ustring>& texts = tk.texts;
  std::size_t i = 0;
  // `types[i]`, with -1 past the end in place of undefined.
  const auto type_at = [&](std::size_t k) { return k < types.size() ? types[k] : -1; };
  const auto text_at = [&](std::size_t k) -> const ustring& {
    static const ustring none;
    return k < texts.size() ? texts[k] : none;
  };

  const auto fail = [&](const std::string& expected) {
    const std::size_t at = std::min(i, tk.starts.size() - 1);
    const int t = type_at(i);
    if (t == T_ERROR) throw syntax_error("class", src, tk.starts[at], u"", true);
    throw syntax_error("class", src, tk.starts[at], to_utf16("Expecting " + expected + ", got '" + token_name(t) + "'"));
  };

  const auto expect = [&](int type) -> ustring {
    if (type_at(i) != type) fail(std::string("'") + token_name(type) + "'");
    return text_at(i++);
  };

  const auto take_alphanum = [&]() -> ustring {
    if (!alphanum(type_at(i))) fail("'UNICODE_TEXT', 'NUM', 'ALPHA', 'MINUS'");
    return text_at(i++);
  };

  const auto name = [&](bool generic) -> ustring {
    ustring out;
    for (;;) {
      if (type_at(i) == T_BQUOTE_STR) {
        out += text_at(i++);
        break;
      }
      if (!alphanum(type_at(i))) fail(kNameTokens);
      out += text_at(i++);
      const int t = type_at(i);
      if (t == T_DOT) {
        out += u'.';
        i++;
      } else if (!alphanum(t) && t != T_BQUOTE_STR) {
        break;
      }
    }
    if (generic && type_at(i) == T_GENERICTYPE) out += u"~" + text_at(i++) + u"~";
    return out;
  };

  const auto class_label = [&]() -> ustring {
    i++;
    const ustring label = expect(T_STR);
    expect(T_SQE);
    return label;
  };

  const auto members = [&]() -> std::vector<ustring> {
    if (type_at(i) != T_MEMBER) fail("'STRUCT_STOP', 'MEMBER'");
    std::vector<ustring> list;
    while (type_at(i) == T_MEMBER) list.push_back(text_at(i++));
    expect(T_STRUCT_STOP);
    // The grammar collects members last to first, and addMembers turns them back.
    std::reverse(list.begin(), list.end());
    return list;
  };

  const auto class_statement = [&]() -> ustring {
    i++;
    const ustring id = name(true);
    db.add_class(id);
    if (type_at(i) == T_SQS) db.set_class_label(id, class_label());
    switch (type_at(i)) {
      case T_STYLE_SEPARATOR: {
        i++;
        const ustring css = take_alphanum();
        db.set_css_class(id, css);
        if (type_at(i) == T_STRUCT_START) {
          i++;
          db.add_members(id, members());
        }
        break;
      }
      case T_STRUCT_START:
        i++;
        if (type_at(i) == T_STRUCT_STOP) i++;
        else db.add_members(id, members());
        break;
      case T_ANNOTATION_START: {
        i++;
        const ustring annotation = take_alphanum();
        expect(T_ANNOTATION_END);
        db.add_annotation(id, annotation);
        if (type_at(i) == T_STRUCT_START) {
          i++;
          if (type_at(i) == T_STRUCT_STOP) i++;
          else db.add_members(id, members());
        }
        break;
      }
    }
    return id;
  };

  const auto note_statement = [&]() -> ustring {
    if (type_at(i++) == T_NOTE) return db.add_note(expect(T_STR));
    const ustring id = name(true);
    return db.add_note(expect(T_STR), id);
  };

  // Namespaces nest, so they are parsed with a stack of open ones.
  const auto namespace_statement = [&]() {
    std::vector<frame> open;
    for (;;) {
      i++;
      const ustring ns = name(false);
      const ustring id = type_at(i) == T_SQS ? db.add_namespace(ns, class_label()) : db.add_namespace(ns);
      expect(T_STRUCT_START);
      if (type_at(i) == T_NEWLINE) i++;
      open.push_back({id, {}, {}});
      bool item = true;
      while (item) {
        frame& current = open.back();
        const int t = type_at(i);
        if (t == T_NAMESPACE) break;
        if (t == T_CLASS) current.classes.push_back(class_statement());
        else if (t == T_NOTE || t == T_NOTE_FOR) current.notes.push_back(note_statement());
        else fail("'NAMESPACE', 'CLASS', 'NOTE_FOR', 'NOTE'");
        for (;;) {
          if (type_at(i) == T_NEWLINE) {
            const int next = type_at(i + 1);
            if (next == T_NAMESPACE || next == T_CLASS || next == T_NOTE || next == T_NOTE_FOR) {
              i++;
              break;
            }
            i++;
          }
          expect(T_STRUCT_STOP);
          const frame done = open.back();
          open.pop_back();
          db.add_classes_to_namespace(done.id, done.classes, done.notes);
          db.pop_namespace();
          if (open.empty()) {
            item = false;
            break;
          }
        }
      }
      if (open.empty()) return;
    }
  };

  const auto relation_end = [&]() -> int {
    const int type = end_type(type_at(i));
    if (type == 0) return kEndNone;
    i++;
    return type - 1;
  };

  const auto class_or_relation = [&]() {
    const ustring id1 = name(true);
    int t = type_at(i);
    if (t == T_LABEL) {
      db.add_member(id1, db.cleanup_label(text_at(i++)));
      return;
    }
    ustring title1 = u"none";
    if (t == T_STR) {
      title1 = text_at(i++);
      t = type_at(i);
    } else if (end_type(t) == 0 && t != T_LINE && t != T_DOTTED_LINE) {
      return;
    }
    const int type1 = relation_end();
    t = type_at(i);
    if (t != T_LINE && t != T_DOTTED_LINE) fail("'AGGREGATION', 'EXTENSION', 'COMPOSITION', 'DEPENDENCY', 'LOLLIPOP', 'LINE', 'DOTTED_LINE'");
    i++;
    const int type2 = relation_end();
    const ustring title2 = type_at(i) == T_STR ? text_at(i++) : ustring(u"none");
    const ustring id2 = name(true);
    class_relation relation;
    relation.id1 = id1;
    relation.id2 = id2;
    relation.type1 = type1;
    relation.type2 = type2;
    relation.line_type = t == T_LINE ? 0 : 1;
    relation.relation_title1 = title1;
    relation.relation_title2 = title2;
    if (type_at(i) == T_LABEL) relation.title = db.cleanup_label(text_at(i++));
    db.add_relation(std::move(relation));
  };

  const auto styles = [&]() -> std::vector<ustring> {
    std::vector<ustring> out;
    for (;;) {
      if (!style_part(type_at(i))) fail("'ALPHA', 'NUM', 'COLON', 'UNIT', 'SPACE', 'BRKT', 'STYLE', 'PCT', 'LABEL'");
      ustring s = text_at(i++);
      while (style_part(type_at(i))) s += text_at(i++);
      out.push_back(s);
      if (type_at(i) != T_COMMA) return out;
      i++;
    }
  };

  const auto tooltip = [&](const ustring& id) {
    if (type_at(i) == T_STR) db.set_tooltip(id, text_at(i++));
  };

  const auto link_statement = [&](const ustring& id) {
    const ustring href = expect(T_STR);
    std::optional<ustring> tip;
    if (type_at(i) == T_STR) tip = text_at(i++);
    if (type_at(i) == T_LINK_TARGET) db.set_link(id, href, text_at(i++));
    else db.set_link(id, href);
    if (tip) db.set_tooltip(id, tip);
  };

  const auto statement = [&]() {
    const int t = type_at(i);
    switch (t) {
      case T_UNICODE_TEXT:
      case T_ALPHA:
      case T_MINUS:
      case T_BQUOTE_STR:
        class_or_relation();
        return;
      case T_NAMESPACE:
        namespace_statement();
        return;
      case T_CLASS:
        class_statement();
        return;
      case T_MEMBER:
        i++;
        return;
      case T_ANNOTATION_START: {
        i++;
        const ustring annotation = take_alphanum();
        expect(T_ANNOTATION_END);
        db.add_annotation(name(true), annotation);
        return;
      }
      case T_CALLBACK: {
        i++;
        const ustring id = name(true);
        db.set_click_event(id);
        expect(T_STR);
        tooltip(id);
        return;
      }
      case T_LINK:
        i++;
        link_statement(name(true));
        return;
      case T_CLICK: {
        i++;
        const ustring id = name(true);
        if (type_at(i) == T_HREF) {
          i++;
          link_statement(id);
          return;
        }
        expect(T_CALLBACK_NAME);
        if (type_at(i) == T_CALLBACK_ARGS) i++;
        db.set_click_event(id);
        tooltip(id);
        return;
      }
      case T_STYLE: {
        i++;
        const ustring id = expect(T_ALPHA);
        db.set_css_style(id, styles());
        return;
      }
      case T_CSSCLASS: {
        i++;
        const ustring ids = expect(T_STR);
        db.set_css_class(ids, expect(T_ALPHA));
        return;
      }
      case T_NOTE:
      case T_NOTE_FOR:
        note_statement();
        return;
      case T_CLASSDEF: {
        i++;
        std::vector<ustring> ids{expect(T_ALPHA)};
        while (type_at(i) == T_COMMA) {
          i++;
          ids.push_back(expect(T_ALPHA));
        }
        db.define_class(ids, styles());
        return;
      }
      case T_direction_tb:
      case T_direction_bt:
      case T_direction_rl:
      case T_direction_lr:
        i++;
        db.set_direction(t == T_direction_tb ? u"TB" : t == T_direction_bt ? u"BT" : t == T_direction_rl ? u"RL" : u"LR");
        return;
      case T_acc_title:
        i++;
        db.set_acc_title(trim(expect(T_acc_title_value)));
        return;
      case T_acc_descr:
        i++;
        db.set_acc_description(trim(expect(T_acc_descr_value)));
        return;
      case T_acc_descr_multiline_value:
        db.set_acc_description(trim(text_at(i++)));
        return;
    }
    fail("'acc_title', 'acc_descr', 'acc_descr_multiline_value', 'NAMESPACE', 'ANNOTATION_START', 'CLASS', 'MEMBER', 'NOTE_FOR', 'NOTE', 'CLASSDEF', 'direction_tb', 'CALLBACK', 'LINK', 'CLICK', 'STYLE', 'CSSCLASS', 'ALPHA', 'MINUS', 'BQUOTE_STR'");
  };

  // Mermaid's grammar also accepts statements with no `classDiagram` line before them.
  const bool header = type_at(i) == T_CLASS_DIAGRAM;
  if (header) {
    i++;
    expect(T_NEWLINE);
  }
  for (;;) {
    statement();
    if (type_at(i) != T_NEWLINE) break;
    i++;
    if (type_at(i) == T_EOF || type_at(i) == T_END) break;
  }
  if (header) expect(T_EOF);
  expect(T_END);
  (void)kCount;
}

}  // namespace pele::classdiagram
