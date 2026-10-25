cmake_minimum_required(VERSION 3.16)
project(pele_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Identical inputs must give identical SVG, so floating point must not be fused or reordered.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-ffp-contract=off -Wall -Wextra -Wshadow)
elseif(MSVC)
  add_compile_options(/fp:precise /W4 /utf-8)
endif()

# `uml` target (clang-uml class diagrams, see the end of this file). It needs compile_commands.json,
# which has to be requested before the targets are created.
option(PELE_ENABLE_UML "Add the 'uml' target (class diagrams with clang-uml)" OFF)
if(PELE_ENABLE_UML)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

add_library(pele
  src/layered.cpp
  src/compound.cpp
  src/ustring.cpp
  src/text_measurer.cpp
  src/entities.cpp
  src/label.cpp
  src/svg.cpp
  src/theme.cpp
  src/edges.cpp
  src/cluster.cpp
  src/shapes.cpp
  src/title.cpp
  src/svg_document.cpp
  src/errors.cpp
  src/detect.cpp
  src/yaml.cpp
  src/preprocess.cpp
  src/regex.cpp
  src/url.cpp
  src/common/tokens.cpp
  src/common/fit_text.cpp
  src/common/literal_label.cpp
  src/common/outline.cpp
  src/common/icon.cpp
  src/lowercase.cpp
  src/js_format.cpp
  src/kanban/kanban.cpp
  src/mindmap/mindmap.cpp
  src/pie/pie.cpp
  src/treemap/format.cpp
  src/treemap/treemap.cpp
  src/info/info.cpp
  src/packet/packet.cpp
  src/flowchart/lexer.cpp
  src/flowchart/shape_names.cpp
  src/flowchart/db.cpp
  src/flowchart/parser.cpp
  src/flowchart/graph.cpp
  src/flowchart/render.cpp
  src/diagram.cpp
  src/core.cpp
  src/fit_width.cpp
)
add_library(pele::pele ALIAS pele)
target_include_directories(pele PUBLIC include PRIVATE include/pele src src/flowchart)

option(PELE_BUILD_TESTS "Build tests" ON)
if(PELE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

option(PELE_BUILD_CLI "Build the pele-cli tool" ON)
if(PELE_BUILD_CLI)
  add_executable(pele-cli tools/pele_cli.cpp)
  target_link_libraries(pele-cli PRIVATE pele)
endif()

# -----------------------------------------------------------------------------
# UML: `cmake --build build --target uml`
#   cmake -B build -G Ninja -DPELE_ENABLE_UML=ON [-DPELE_UML_DIAGRAM=diagrams_class]
#         [-DPELE_UML_GENERATOR=plantuml|mermaid|json] [-DPELE_UML_FORMAT=svg|png|pdf]
# Needs clang-uml, and plantuml (or mmdc for mermaid) to draw the result. The diagrams are listed
# in tools/uml/clang-uml.in.yaml and are written to <build>/diagrams.
# -----------------------------------------------------------------------------
set(PELE_UML_DIAGRAM "core_class" CACHE STRING "clang-uml diagram name")
set(PELE_UML_GENERATOR "plantuml" CACHE STRING "plantuml | mermaid | json")
set_property(CACHE PELE_UML_GENERATOR PROPERTY STRINGS plantuml mermaid json)
set(PELE_UML_TU "${CMAKE_CURRENT_SOURCE_DIR}/tools/uml/uml_tu.cpp" CACHE FILEPATH "A TU present in compile_commands.json")
set(PELE_UML_FORMAT "svg" CACHE STRING "UML output format (svg, png, pdf)")
set_property(CACHE PELE_UML_FORMAT PROPERTY STRINGS svg png pdf)

if(PELE_ENABLE_UML)
  # Only built by the `uml` target, so an ordinary build does not compile it.
  add_library(uml_tu OBJECT EXCLUDE_FROM_ALL ${PELE_UML_TU})
  # Only the headers are read, so the library itself need not be built first.
  target_include_directories(uml_tu PRIVATE include include/pele src src/flowchart)

  find_program(CLANG_UML_EXECUTABLE NAMES clang-uml REQUIRED)

  # Paths
  set(PELE_UML_CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/uml/clang-uml.in.yaml")
  set(PELE_UML_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/.clang-uml")
  set(PELE_UML_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/diagrams")
  set(PELE_UML_COMPILATION_DATABASE_DIR "${CMAKE_BINARY_DIR}")

  file(MAKE_DIRECTORY "${PELE_UML_OUTPUT_DIR}")

  # Generate .clang-uml in the build directory
  configure_file("${PELE_UML_CONFIG_TEMPLATE}" "${PELE_UML_CONFIG}" @ONLY)

  set(CLANG_UML_CMD
      "${CLANG_UML_EXECUTABLE}" -v
      -g "${PELE_UML_GENERATOR}"
      -c "${PELE_UML_CONFIG}"
      -n "${PELE_UML_DIAGRAM}"
  )

  # PlantUML
  if(PELE_UML_GENERATOR STREQUAL "plantuml")
    find_program(PLANTUML_EXECUTABLE NAMES plantuml REQUIRED)
    set(PELE_UML_INPUT "${PELE_UML_OUTPUT_DIR}/${PELE_UML_DIAGRAM}.puml")
    set(PELE_UML_OUTPUT "${PELE_UML_OUTPUT_DIR}/${PELE_UML_DIAGRAM}.${PELE_UML_FORMAT}")
    set(PELE_UML_RENDER_CMD
        "${PLANTUML_EXECUTABLE}" -v
        "-t${PELE_UML_FORMAT}"
        "${PELE_UML_INPUT}"
    )
  # Mermaid
  elseif(PELE_UML_GENERATOR STREQUAL "mermaid")
    find_program(MMDC_EXECUTABLE NAMES mmdc REQUIRED)
    set(PELE_UML_INPUT "${PELE_UML_OUTPUT_DIR}/${PELE_UML_DIAGRAM}.mmd")
    set(PELE_UML_OUTPUT "${PELE_UML_OUTPUT_DIR}/${PELE_UML_DIAGRAM}.${PELE_UML_FORMAT}")
    set(PELE_UML_RENDER_CMD
        "${MMDC_EXECUTABLE}"
        -i "${PELE_UML_INPUT}"
        -o "${PELE_UML_OUTPUT}"
    )
  # Json
  elseif(PELE_UML_GENERATOR STREQUAL "json")
    set(PELE_UML_INPUT "${PELE_UML_OUTPUT_DIR}/${PELE_UML_DIAGRAM}.json")
    set(PELE_UML_OUTPUT "${PELE_UML_INPUT}")
    set(PELE_UML_RENDER_CMD "${CMAKE_COMMAND}" -E echo "json is ready!")
  else()
    message(FATAL_ERROR "PELE_UML_GENERATOR must be plantuml, mermaid or json")
  endif()

  add_custom_target(uml
      COMMAND ${CLANG_UML_CMD}
      COMMAND ${PELE_UML_RENDER_CMD}
      COMMAND "${CMAKE_COMMAND}" -E echo "-- [UML] Output written to: ${PELE_UML_OUTPUT}"
      WORKING_DIRECTORY "${PELE_UML_OUTPUT_DIR}"
      COMMENT "Generate UML (${PELE_UML_DIAGRAM}) with ${PELE_UML_GENERATOR} (${PELE_UML_FORMAT})"
      VERBATIM
  )
  add_dependencies(uml uml_tu)
endif()
