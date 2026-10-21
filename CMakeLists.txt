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
  add_compile_options(-ffp-contract=off -Wall -Wextra)
elseif(MSVC)
  add_compile_options(/fp:precise /W4)
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
)
add_library(pele::pele ALIAS pele)
target_include_directories(pele PUBLIC include PRIVATE src)

option(PELE_BUILD_TESTS "Build tests" ON)
if(PELE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
