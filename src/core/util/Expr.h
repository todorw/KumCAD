#pragma once

#include <functional>
#include <optional>
#include <string>

namespace lcad {

// AutoCAD CAL/QuickCalc-style arithmetic expression evaluator: +, -, *, /,
// ^ (exponent, right-associative), unary +/-, parentheses, the constants pi
// and e, and functions sin/cos/tan/asin/acos/atan/atan2/sqrt/abs/ln/log/exp/
// min/max. Trig functions take/return degrees (AutoCAD CAL's default angle
// unit), not radians. Returns nullopt with *errorOut set on a malformed
// expression (unknown token, mismatched parens, wrong argument count,
// division by zero, domain error).
std::optional<double> evaluateExpression(const std::string& expr, std::string* errorOut = nullptr);

// Resolves a bare identifier that isn't "pi"/"e" and isn't followed by
// '(' (a function call) to a value, or nullopt if the name is unknown --
// the hook FreeCAD-style named document variables (see core/core3d/
// Document3D.h) plug in through. Unlike "pi"/"e" and function names
// (case-folded, AutoCAD CAL's own convention), the name passed here is
// the ORIGINAL, un-lowercased text, so variable names stay case-
// sensitive as the user actually typed them (e.g. "Width" and "width"
// are different variables) -- matching FreeCAD's own expression engine.
using VariableLookup = std::function<std::optional<double>(const std::string&)>;

// Same as evaluateExpression, but an identifier that isn't a built-in
// constant/function is resolved via lookup instead of immediately
// failing with "unknown identifier".
std::optional<double> evaluateExpression(const std::string& expr, const VariableLookup& lookup,
                                         std::string* errorOut = nullptr);

} // namespace lcad
