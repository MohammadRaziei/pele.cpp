#pragma once

namespace pele {

// Whether a UTF-16 code unit is in the ranges of letters that Pele's lexers accept (up to U+FFDC),
// as `isUnicodeLetter` in util/unicode.ts.
bool is_unicode_letter(int code);

}  // namespace pele
