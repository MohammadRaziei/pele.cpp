// Compares url.h with Pele's TypeScript (tests/data/url_cases.txt, made by tools/gen_url_cases.mjs).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "url.h"

using namespace pele;

namespace {

ustring unhex16(const std::string& s) {
  ustring out;
  for (std::size_t i = 1; i + 4 <= s.size(); i += 4) out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

std::string unhex8(const std::string& s) {
  std::string out;
  for (std::size_t i = 1; i + 2 <= s.size(); i += 2) out += static_cast<char>(std::stoul(s.substr(i, 2), nullptr, 16));
  return out;
}

// The options the generator wrote as JSON: a few fixed shapes.
render_options options_of(const std::string& json) {
  render_options o;
  if (json.find("\"links\":false") != std::string::npos) o.links = false;
  if (json.find("\"images\":false") != std::string::npos) o.images = false;
  if (json.find("\"linkSchemes\":[\"https\"]") != std::string::npos) o.link_schemes = std::vector<std::string>{"https"};
  if (json.find("\"linkSchemes\":[\"http\",\"FTP\"]") != std::string::npos) o.link_schemes = std::vector<std::string>{"http", "FTP"};
  if (json.find("\"imageSchemes\":[\"https\",\"data\"]") != std::string::npos) o.image_schemes = std::vector<std::string>{"https", "data"};
  if (json.find("\"linkRel\":\"\"") != std::string::npos) o.link_rel = "";
  if (json.find("\"linkRel\":\"nofollow noopener\"") != std::string::npos) o.link_rel = "nofollow noopener";
  return o;
}

std::string show(const std::optional<ustring>& v) { return v ? to_utf8(*v) : "-"; }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1]);
  std::string line;
  int total = 0, failed = 0, shown = 0;
  while (std::getline(in, line)) {
    std::istringstream s(line);
    std::string kind, url, sanitized, safe, link, image, rel, opts;
    s >> kind >> url >> sanitized >> safe >> link >> image >> rel >> opts;
    if (kind != "url") continue;
    const ustring u = unhex16(url);
    const render_options o = options_of(unhex8(opts));
    const ustring got_sanitized = sanitize_url(u);
    std::string why;
    if (got_sanitized != unhex16(sanitized)) why = "sanitize_url: got [" + to_utf8(got_sanitized) + "] want [" + to_utf8(unhex16(sanitized)) + "]";
    else if (safe_url(u) != unhex16(safe)) why = "safe_url";
    else if (show(link_url(got_sanitized, o)) != (link == "-" ? "-" : to_utf8(unhex16(link)))) why = "link_url";
    else if (show(image_url(got_sanitized, o)) != (image == "-" ? "-" : to_utf8(unhex16(image)))) why = "image_url";
    else if (rel_attr(o) != unhex8(rel)) why = "rel_attr";
    total++;
    if (!why.empty()) {
      failed++;
      if (shown++ < 8) std::cout << why << "\n  url: " << to_utf8(u) << "\n";
    }
  }
  std::cout << (total - failed) << "/" << total << " cases match\n";
  return failed == 0 ? 0 : 1;
}
