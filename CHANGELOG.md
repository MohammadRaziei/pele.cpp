# Changelog

The version of this library follows its own numbering. Which version of Pele (the TypeScript
library) its SVG matches byte for byte is in `PELE_CPP_REFERENCE_VERSION` (`pele/version.h`).

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
