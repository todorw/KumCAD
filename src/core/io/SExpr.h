#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lcad {

// A generic S-expression tree -- the shared syntax underlying every real
// KiCad file format this codebase talks to (.kicad_pcb, .kicad_mod,
// .kicad_sch, .kicad_sym, .kicad_pro): nested parenthesized lists of
// symbols, quoted strings, and numbers, e.g.
//   (footprint "R_0603" (layer "F.Cu") (at 10 20 90))
// This is deliberately just the generic tree, not a KiCad-specific
// schema -- KiCadMod.h/KiCadPcb.h build the actual footprint/board object
// mapping on top of it, the same layering DxfReader/DxfWriter keep
// between "parse group codes" and "build entities."
struct SExpr {
    enum class Kind { List, Symbol, String, Number };
    Kind kind = Kind::List;
    std::string text;    // Symbol name, or String's unescaped contents
    double number = 0.0; // valid when kind == Number
    std::vector<SExpr> items; // valid when kind == List (the list's own children)

    bool isList() const { return kind == Kind::List; }
    bool isSymbol() const { return kind == Kind::Symbol; }
    bool isString() const { return kind == Kind::String; }
    bool isNumber() const { return kind == Kind::Number; }

    // For a list node whose first child is a bare Symbol (e.g. "footprint"
    // in (footprint "R_0603" ...)), that symbol's text -- KiCad's own
    // convention for tagging what kind of list this is. Empty if this
    // isn't a list, the list is empty, or its first child isn't a Symbol.
    std::string tag() const;

    // The first direct child list whose tag() == name, or nullptr.
    const SExpr* child(const std::string& name) const;
    // Every direct child list whose tag() == name, in order.
    std::vector<const SExpr*> children(const std::string& name) const;

    // Convenience accessors for a tagged child list's own remaining
    // items (skipping the tag symbol itself), e.g. for (at 10 20 90),
    // atF(0)==10, atF(1)==20, atF(2)==90. Returns def if index is out of
    // range or that item isn't a Number.
    double numberAt(std::size_t index, double def = 0.0) const;
    // Same idea for a String or bare Symbol item (KiCad often leaves
    // enum-like tokens such as "smd"/"yes" unquoted).
    std::string textAt(std::size_t index, const std::string& def = "") const;

    static SExpr sym(std::string s);
    static SExpr str(std::string s);
    static SExpr num(double v);
    static SExpr list(std::string tag, std::vector<SExpr> rest = {});
};

// Parses text as a single top-level S-expression (a whole KiCad file is
// one list, e.g. (kicad_pcb ...)). Returns std::nullopt on a malformed
// expression: unbalanced parens, an unterminated string, or trailing
// non-whitespace content after the first complete expression.
std::optional<SExpr> parseSExpr(const std::string& text);

// Pretty-prints expr close to real KiCad's own formatting: each list
// child that is itself a list starts on its own indented line (two
// spaces per nesting depth); consecutive leaf atoms (symbols/strings/
// numbers) stay inline with their parent's opening paren. This matches
// KiCad's own writer closely enough to diff sanely against a real KiCad-
// saved file, though KiCad's exact pretty-printer is undocumented and has
// shifted across versions, so byte-for-byte identity isn't promised.
std::string writeSExpr(const SExpr& expr);

// Formats a double the way KiCad's own writer does: fixed notation,
// trimmed to remove trailing zeros/dot, defaulting to 6 decimal places
// of precision (KiCad's own internal unit is nm but its file format
// still prints mm with up to 6 decimals).
std::string formatKiCadNumber(double value);

} // namespace lcad
