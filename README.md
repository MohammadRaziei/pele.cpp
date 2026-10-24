# pele.cpp

Mermaid diagrams as themeable SVG, in C++17 with no dependencies. A rewrite of
[Pele](https://github.com/obsidianmd/pele): the same text gives the same SVG.

```cpp
#include <pele/pele.h>

pele::render_result r = pele::render("flowchart TD\n  A[Text] --> B{Choice}\n  B -->|yes| C[Done]");
std::cout << r.svg;
```

Errors are thrown as `pele::pele_error` (`code()`, `line()`, `column()`, `snippet()`). To draw one
text several times, read it once with `pele::parse()` and pass the result to `pele::render()`.

From the command line: `pele-cli diagram.mmd > diagram.svg`.

## Colours

The SVG draws everything with CSS variables that have fallbacks (`--pele-bg`, `--pele-fg`,
`--pele-line`, `--pele-surface`, ...), so a page themes it, light or dark, without drawing it again.

## What is done

| Diagram | State |
| --- | --- |
| flowchart | done: byte-for-byte the SVG that `pele.js` writes |

The other types are listed in `checklist.md`.

## How it is built, and how it is checked

* Text is UTF-16 inside, as in JavaScript, since widths, wrapping and scanning count UTF-16 code units.
* Floating point is not fused or reordered (`-ffp-contract=off`), and `Math.hypot` follows V8, so
  coordinates agree to the last bit.
* Each part has a test that runs the TypeScript original on random input and compares the result
  (`tools/gen_*_cases.mjs`, `tests/`). `tests/test_e2e.cpp` compares whole diagrams and needs only
  the manifests `tests/data/e2e*.txt` (one line per diagram, with the SHA-256 of the SVG Pele's
  TypeScript gave).
* Text is measured with a table of Arial widths, the same everywhere. Give `render_options::measurer`
  your own `pele::text_measurer` to use real font metrics.

## Layout of the code

* `include/pele/` is the public API. Everything is in `namespace pele`.
* `src/` has the shared parts: text and label layout, SVG helpers, shapes, edge routing, and the
  layered and compound layouts (behind the `flat_layout_engine` interface).
* `tools/uml/` is the clang-uml setup behind the `uml` target (see Class diagrams).
* `src/flowchart/` is one diagram type. A type is a `pele::diagram` that is added to the
  `diagram_registry`.

## Build

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Class diagrams

`uml` draws the classes with [clang-uml](https://clang-uml.github.io/), from the headers listed in
`tools/uml/uml_tu.cpp` and the diagrams in `tools/uml/clang-uml.in.yaml`. It is off by default,
because it needs clang-uml (and PlantUML, or `mmdc` for Mermaid) installed.

```sh
cmake -S . -B build -G Ninja -DPELE_ENABLE_UML=ON      # Ninja or Make: it needs compile_commands.json
cmake --build build --target uml                         # build/diagrams/core_class.svg
cmake -S . -B build -DPELE_UML_DIAGRAM=diagrams_class    # the diagram types' own classes
```

`PELE_UML_GENERATOR` is `plantuml` (default), `mermaid` or `json`; `PELE_UML_FORMAT` is `svg`
(default), `png` or `pdf`.

MIT licence, like Pele.
