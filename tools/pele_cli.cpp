// pele-cli: reads Mermaid text from a file (or standard input) and writes the SVG.
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include <pele/pele.h>

int main(int argc, char** argv) {
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
