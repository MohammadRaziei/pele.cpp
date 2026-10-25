#include "js_format.h"

#include <charconv>
#include <cmath>

#include "js_math.h"

namespace pele {
namespace {

// The exact decimal digits of a positive finite double, as d0 d1 d2 ... with x = d0.d1d2... * 10^exponent.
// A double has at most 767 significant digits.
struct decimal {
  std::string digits;
  int exponent = 0;
};

decimal exact(double x) {
  char buf[800];
  const auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific, 770);
  const std::string text(buf, r.ptr);
  const std::size_t e = text.find('e');
  decimal d;
  for (std::size_t i = 0; i < e; i++)
    if (text[i] != '.') d.digits += text[i];
  d.exponent = std::stoi(text.substr(e + 1));
  return d;
}

// Keeps `count` significant digits, rounding a remainder of half or more up. May carry into a new
// leading digit, which raises the exponent. The digits are padded with zeros when they are fewer.
decimal round_to(const decimal& d, std::size_t count) {
  decimal out;
  out.exponent = d.exponent;
  out.digits = d.digits.substr(0, count);
  while (out.digits.size() < count) out.digits += '0';
  if (d.digits.size() > count && d.digits[count] >= '5') {
    std::size_t i = count;
    while (i > 0 && out.digits[i - 1] == '9') out.digits[--i] = '0';
    if (i == 0) {
      out.digits.insert(out.digits.begin(), '1');
      out.digits.pop_back();
      out.exponent++;
    } else {
      out.digits[i - 1]++;
    }
  }
  return out;
}

std::string special(double x, bool& done) {
  done = true;
  if (std::isnan(x)) return "NaN";
  if (std::isinf(x)) return "Infinity";
  done = false;
  return std::string();
}

std::string exponent_text(int e) { return std::string("e") + (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e); }

}  // namespace

std::string js_to_fixed(double x, int digits) {
  bool done;
  std::string s = special(x, done);
  if (done) return s;
  if (x >= 1e21) return js_number_string(x);
  // Integer and fraction digits as they are written, exactly: a double has at most 1074 after the point.
  char buf[1500];
  const auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, 1100);
  const std::string text(buf, r.ptr);
  const std::size_t dot = text.find('.');
  std::string whole = text.substr(0, dot);
  const std::string fraction = text.substr(dot + 1);
  std::string kept = whole + fraction.substr(0, static_cast<std::size_t>(digits));
  if (fraction[static_cast<std::size_t>(digits)] >= '5') {
    std::size_t i = kept.size();
    while (i > 0 && kept[i - 1] == '9') kept[--i] = '0';
    if (i == 0) kept.insert(kept.begin(), '1');
    else kept[i - 1]++;
  }
  const std::size_t split = kept.size() - static_cast<std::size_t>(digits);
  std::string integer = kept.substr(0, split);
  // Leading zeros of the integer part are dropped, as JavaScript writes "0.5" and not "00.5".
  std::size_t lead = 0;
  while (lead + 1 < integer.size() && integer[lead] == '0') lead++;
  integer = integer.substr(lead);
  return digits == 0 ? integer : integer + "." + kept.substr(split);
}

std::string js_to_exponential(double x, int digits) {
  bool done;
  std::string s = special(x, done);
  if (done) return s;
  if (x == 0) return (digits > 0 ? "0." + std::string(static_cast<std::size_t>(digits), '0') : std::string("0")) + "e+0";
  const decimal d = round_to(exact(x), static_cast<std::size_t>(digits) + 1);
  return d.digits.substr(0, 1) + (digits > 0 ? "." + d.digits.substr(1) : std::string()) + exponent_text(d.exponent);
}

std::string js_to_precision(double x, int digits) {
  bool done;
  std::string s = special(x, done);
  if (done) return s;
  const std::size_t p = static_cast<std::size_t>(digits);
  decimal d;
  if (x == 0) {
    d.digits = std::string(p, '0');
  } else {
    d = round_to(exact(x), p);
  }
  const int e = d.exponent;
  if (e < -6 || e >= digits) return d.digits.substr(0, 1) + (p > 1 ? "." + d.digits.substr(1) : std::string()) + exponent_text(e);
  if (e == digits - 1) return d.digits;
  if (e >= 0) return d.digits.substr(0, static_cast<std::size_t>(e) + 1) + "." + d.digits.substr(static_cast<std::size_t>(e) + 1);
  return "0." + std::string(static_cast<std::size_t>(-(e + 1)), '0') + d.digits;
}

}  // namespace pele
