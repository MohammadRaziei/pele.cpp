# Changelog

The version of this library follows its own numbering. Which version of Pele (the TypeScript
library) its SVG matches byte for byte is in `PELE_CPP_REFERENCE_VERSION` (`pele/version.h`).

## 0.3.0

Matches Pele 1.11.0 (was 1.10.0). Pele 1.12.0 exists and is not followed yet.

Changed to match Pele
- Error positions: an error at the end of the text is reported on the last line with content, and a
  token error at the end is placed after the last visible token.
- Scanners get the type of the previous token (`token_type::match`/`scanner`); `title_before` is a
  TITLE token that does not split a longer name (treemap uses it).
- Flowchart: a subgraph an edge crosses into or out of takes its parent's direction, as in Mermaid.
- Treemap: the gap is dropped when it would erase a box; boxes are clamped to their parent; small
  sections give up their header (shorter header, or none, with the children filling the box and the
  tooltip showing the path); gaps and insets shrink as leaves get smaller; corners are capped on
  small boxes; a class `font-size`, bold or italic is honored by the labels.
- Class: `render_options::monospace` containing "members" draws members in the monospace font
  unless a class sets its own `font-family`.

Tests
- The treemap generator makes dense trees and `font-size` classes; the class generator uses
  `monospace` and `font-family` styles. Checked on 5,400 further random diagrams of the ported types.

## 0.2.0

Matches Pele 1.10.0 (was 1.7.0). Adds the class diagram. Newer Pele versions exist (1.10.1, 1.11.0); not yet followed.

Changed to match Pele
- Rounded corners are written as `style="rx:var(--_r)"` instead of an `rx` attribute.
- Labels: `<sub>`, `<sup>`, `<u>`, `<code>` (also `kbd`, `samp`, `tt`) and the other HTML tags Pele
  drops; soft hyphens and zero-width spaces are line break hints; wrapped lines sit closer
  (line height 1.3 instead of 1.5).
- SVG text drops characters invalid in XML, including lone surrogates; `<span>` styles for mono,
  sub, sup and underline.
- Entity codes are restored once, as Mermaid does, and shown as written in error messages.
- Errors point at the position in the source as written: preprocessing edits (front matter,
  directives, comments, entity codes, color semicolons, whitespace after a brace) are mapped back.
- Layout: groups kept contiguous in ranks, fixed port ranks, an adjacent-swap pass after the
  crossing sweeps, and for subgraphs a flat layout that orders nodes and ports across groups, with
  route crossing counts choosing between orderings.
- Edge limit message: "Edge limit exceeded. The limit is N."
- c4 detection anchors every keyword.

New
- class diagram (graph, markers, render, and `wrap_components` for disconnected parts when the host
  gives a `maxWidth`).

Not ported
- Math (`$$...$$`), which Pele only uses when given a math renderer. A class diagram without math
  is complete; `$$` in annotations or cardinalities is drawn as text.

Tests
- The label, kanban, mindmap, treemap and flowchart generators now produce the new tags and break
  hints; the compound generator produces stepped routes and fewer per-group directions.

## 0.1.0

First release. Matches Pele 1.7.0.

Diagram types
- flowchart (with subgraphs, all 58 shapes, markdown labels, icons, links)
- info, packet, pie, kanban, mindmap, treemap, cynefin

Library
- `pele::render(text, options)` returns the SVG, its size, and the links it holds. Errors are
  `pele::pele_error`, worded as Pele words them.
- No dependencies. C++17. A regular expression engine with JavaScript's meaning (`js_regex`), a YAML
  reader, and JavaScript's number formatting (`toFixed`, `toPrecision`, `parseFloat`, ...) are
  part of it, so that the output is the same to the last digit.
- `pele-cli`: reads Mermaid text from a file or standard input and writes the SVG.

Checks
- Every diagram type is compared with `pele.js` on random diagrams, SVG and error messages both.
  `tests/data` holds the regression sets; `tools/gen_*_cases.mjs` make larger ones.

Optional
- `-DPELE_ENABLE_UML=ON` adds a `uml` target (class diagrams of the library with clang-uml).
