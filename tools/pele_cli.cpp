// pele-cli: reads Mermaid text from a file (or standard input) and writes the SVG.
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include <pele/pele.h>
#include <pele/version.h>

int main(int argc, char** argv) {
  if (argc > 1 && (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-V")) {
    std::cout << "pele-cli " PELE_CPP_VERSION " (matches Pele " PELE_CPP_REFERENCE_VERSION ")\n";
    return 0;
  }
  if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    std::cout << "usage: pele-cli [file | -]    reads Mermaid text and writes the SVG\n"
                 "       pele-cli --version\n";
    return 0;
  }
  std::string text;
  if (argc > 1 && std::string(argv[1]) != "-") {
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
      std::cerr << "pele-cli: cannot read " << argv[1] << "\n";
      return 2;
    }
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } else {
    text.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
  }
  try {
    std::cout << pele::render(text).svg << "\n";
  } catch (const pele::pele_error& e) {
    std::cerr << "pele-cli: " << pele::to_string(e.code()) << ": " << e.what() << "\n";
    return 1;
  }
  return 0;
}
