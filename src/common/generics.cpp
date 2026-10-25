#include "common/generics.h"

#include <vector>

namespace pele {
namespace {

std::size_t tildes(ustring_view text) {
  std::size_t count = 0;
  for (char16_t c : text)
    if (c == u'~') count++;
  return count;
}

// Pairs tildes from the outside in: the first with the last, the second with the one before it.
ustring pair_tildes(ustring_view text) {
  const std::size_t count = tildes(text);
  if (count <= 1) return ustring(text);
  const bool lead = count % 2 != 0 && !text.empty() && text[0] == u'~';
  ustring chars(text);
  std::vector<std::size_t> at;
  for (std::size_t i = lead ? 1 : 0; i < chars.size(); i++)
    if (chars[i] == u'~') at.push_back(i);
  for (std::size_t lo = 0, hi = at.size() - 1; lo < hi; lo++, hi--) {
    chars[at[lo]] = u'<';
    chars[at[hi]] = u'>';
  }
  return chars;
}

}  // namespace

ustring parse_generic_types(ustring_view input) {
  if (input.find(u'~') == ustring_view::npos) return ustring(input);
  // `input.split(/(,)/)`: the pieces between commas, with each comma as a piece of its own.
  std::vector<ustring> sets;
  std::size_t from = 0;
  for (;;) {
    const std::size_t comma = input.find(u',', from);
    if (comma == ustring_view::npos) {
      sets.push_back(ustring(input.substr(from)));
      break;
    }
    sets.push_back(ustring(input.substr(from, comma - from)));
    sets.push_back(ustring(u","));
    from = comma + 1;
  }
  std::vector<ustring> output;
  for (std::size_t i = 0; i < sets.size(); i++) {
    ustring set = sets[i];
    if (set == u"," && i > 0 && i + 1 < sets.size() && tildes(sets[i - 1]) == 1 && tildes(sets[i + 1]) == 1) {
      set = sets[i - 1] + u"," + sets[i + 1];
      i++;
      output.pop_back();
    }
    output.push_back(pair_tildes(set));
  }
  ustring joined;
  for (const ustring& piece : output) joined += piece;
  return joined;
}

}  // namespace pele
