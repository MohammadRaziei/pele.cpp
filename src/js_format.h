#pragma once

#include <string>

namespace pele {

// Number.prototype.toFixed, toExponential and toPrecision, for a number that is not negative.
// All three round on the exact value of the double, and a value exactly halfway goes up, where
// printf rounds it to an even digit. `digits` is the argument as JavaScript takes it.
std::string js_to_fixed(double x, int digits);
std::string js_to_exponential(double x, int digits);
std::string js_to_precision(double x, int digits);

}  // namespace pele
