#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lcad {

// FreeCAD's own Spreadsheet workbench: a grid of named cells ("A1",
// "B12", ...), each holding either a literal number, literal text, or a
// formula (leading '=', e.g. "=A1+B2*2") that can reference other
// cells by name -- evaluated via core/util/Expr.h's own arithmetic
// evaluator (the same engine AutoCAD-CAL-style expressions and
// Feature3D::expressions already use elsewhere in this codebase), so a
// cell formula gets the exact same operators/functions/constants for
// free, real reuse rather than a second parser.
//
// A real, disclosed simplification: cells aren't cached/recomputed
// incrementally on a dependency graph -- value() re-evaluates the whole
// formula chain it needs on every call, walking cell references exactly
// like Document3D::applyExpressions already walks variable references.
// Fine for a spreadsheet-sized cell count; not attempted to scale to
// thousands of interlinked cells.
class Spreadsheet {
public:
    // Sets cell's own raw content: a bare literal number or text is
    // stored as-is; content starting with '=' is a formula, evaluated
    // fresh on every value() call. An empty content string clears the
    // cell (same effect as clearCell).
    void setCell(const std::string& cell, std::string content);
    void clearCell(const std::string& cell);
    bool hasCell(const std::string& cell) const;

    // The raw content exactly as set (formula text still has its
    // leading '='), or an empty string if the cell is unset.
    const std::string& rawContent(const std::string& cell) const;

    // The cell's own resolved numeric value: its literal number, or its
    // formula's evaluated result (each cell reference inside resolved
    // recursively, the same way). nullopt if the cell is unset, holds
    // non-numeric literal text, its formula is malformed, references an
    // unknown/non-numeric cell, or is part of a circular reference chain
    // (detected, not an infinite loop) -- with *errorOut set to a
    // human-readable reason when provided.
    std::optional<double> value(const std::string& cell, std::string* errorOut = nullptr) const;

    // Every cell name that currently has content, in no particular order.
    std::vector<std::string> cellNames() const;

private:
    std::optional<double> evaluateCell(const std::string& cell, std::vector<std::string>& visiting,
                                       std::string* errorOut) const;

    std::unordered_map<std::string, std::string> m_cells;
};

} // namespace lcad
