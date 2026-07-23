#include "core/io/SExpr.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace lcad {

std::string SExpr::tag() const {
    if (!isList() || items.empty() || !items.front().isSymbol()) return {};
    return items.front().text;
}

const SExpr* SExpr::child(const std::string& name) const {
    if (!isList()) return nullptr;
    for (const SExpr& it : items) {
        if (it.isList() && it.tag() == name) return &it;
    }
    return nullptr;
}

std::vector<const SExpr*> SExpr::children(const std::string& name) const {
    std::vector<const SExpr*> out;
    if (!isList()) return out;
    for (const SExpr& it : items) {
        if (it.isList() && it.tag() == name) out.push_back(&it);
    }
    return out;
}

double SExpr::numberAt(std::size_t index, double def) const {
    // Index 0 is the tag symbol itself for a tagged list, so callers count
    // from the item AFTER the tag -- offset by 1 to land on items[index+1].
    std::size_t i = index + (tag().empty() ? 0 : 1);
    if (!isList() || i >= items.size() || !items[i].isNumber()) return def;
    return items[i].number;
}

std::string SExpr::textAt(std::size_t index, const std::string& def) const {
    std::size_t i = index + (tag().empty() ? 0 : 1);
    if (!isList() || i >= items.size()) return def;
    const SExpr& it = items[i];
    if (it.isString() || it.isSymbol()) return it.text;
    return def;
}

SExpr SExpr::sym(std::string s) {
    SExpr e;
    e.kind = Kind::Symbol;
    e.text = std::move(s);
    return e;
}

SExpr SExpr::str(std::string s) {
    SExpr e;
    e.kind = Kind::String;
    e.text = std::move(s);
    return e;
}

SExpr SExpr::num(double v) {
    SExpr e;
    e.kind = Kind::Number;
    e.number = v;
    return e;
}

SExpr SExpr::list(std::string tagName, std::vector<SExpr> rest) {
    SExpr e;
    e.kind = Kind::List;
    e.items.push_back(sym(std::move(tagName)));
    for (SExpr& r : rest) e.items.push_back(std::move(r));
    return e;
}

namespace {

bool looksNumeric(const std::string& tok) {
    if (tok.empty()) return false;
    std::size_t i = 0;
    if (tok[i] == '+' || tok[i] == '-') ++i;
    if (i >= tok.size()) return false;
    bool sawDigit = false;
    while (i < tok.size() && std::isdigit(static_cast<unsigned char>(tok[i]))) {
        ++i;
        sawDigit = true;
    }
    if (i < tok.size() && tok[i] == '.') {
        ++i;
        while (i < tok.size() && std::isdigit(static_cast<unsigned char>(tok[i]))) {
            ++i;
            sawDigit = true;
        }
    }
    if (!sawDigit) return false;
    if (i < tok.size() && (tok[i] == 'e' || tok[i] == 'E')) {
        ++i;
        if (i < tok.size() && (tok[i] == '+' || tok[i] == '-')) ++i;
        if (i >= tok.size() || !std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
        while (i < tok.size() && std::isdigit(static_cast<unsigned char>(tok[i]))) ++i;
    }
    return i == tok.size();
}

bool isAtomBreak(char c) {
    return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '"';
}

} // namespace

std::optional<SExpr> parseSExpr(const std::string& text) {
    std::size_t i = 0;
    const std::size_t n = text.size();
    std::vector<SExpr> stack; // open lists, innermost last
    std::optional<SExpr> result;

    auto skipWs = [&]() {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };

    while (true) {
        skipWs();
        if (i >= n) break;
        if (result.has_value() && stack.empty()) {
            // Trailing non-whitespace content after the top-level expression closed.
            return std::nullopt;
        }
        char c = text[i];
        if (c == '(') {
            stack.emplace_back();
            stack.back().kind = SExpr::Kind::List;
            ++i;
        } else if (c == ')') {
            if (stack.empty()) return std::nullopt;
            SExpr done = std::move(stack.back());
            stack.pop_back();
            ++i;
            if (stack.empty()) {
                result = std::move(done);
            } else {
                stack.back().items.push_back(std::move(done));
            }
        } else if (c == '"') {
            ++i;
            std::string s;
            while (i < n && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < n) {
                    char esc = text[i + 1];
                    if (esc == 'n') s += '\n';
                    else if (esc == 't') s += '\t';
                    else s += esc; // \" \\ and anything else pass the escaped char through
                    i += 2;
                } else {
                    s += text[i];
                    ++i;
                }
            }
            if (i >= n) return std::nullopt; // unterminated string
            ++i; // closing quote
            if (stack.empty()) return std::nullopt; // a bare top-level atom isn't a valid file
            stack.back().items.push_back(SExpr::str(std::move(s)));
        } else {
            std::size_t start = i;
            while (i < n && !isAtomBreak(text[i])) ++i;
            std::string tok = text.substr(start, i - start);
            if (stack.empty()) return std::nullopt;
            if (looksNumeric(tok)) {
                stack.back().items.push_back(SExpr::num(std::stod(tok)));
            } else {
                stack.back().items.push_back(SExpr::sym(std::move(tok)));
            }
        }
    }

    if (!stack.empty() || !result.has_value()) return std::nullopt;
    return result;
}

std::string formatKiCadNumber(double value) {
    if (std::abs(value) < 1e-9) value = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", value);
    std::string s(buf);
    // Trim trailing zeros, then a bare trailing '.'.
    std::size_t dot = s.find('.');
    if (dot != std::string::npos) {
        std::size_t last = s.find_last_not_of('0');
        if (last == dot) --last; // drop the dot too if nothing follows it
        s.erase(last + 1);
    }
    if (s == "-0") s = "0";
    return s;
}

namespace {

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

void writeAtom(const SExpr& e, std::ostringstream& out) {
    switch (e.kind) {
        case SExpr::Kind::Symbol: out << e.text; break;
        case SExpr::Kind::String: out << '"' << escapeString(e.text) << '"'; break;
        case SExpr::Kind::Number: out << formatKiCadNumber(e.number); break;
        case SExpr::Kind::List: break; // handled by writeList
    }
}

void writeList(const SExpr& e, std::ostringstream& out, int depth) {
    out << '(';
    bool first = true;
    for (std::size_t idx = 0; idx < e.items.size(); ++idx) {
        const SExpr& child = e.items[idx];
        if (child.isList()) {
            out << '\n' << std::string((depth + 1) * 2, ' ');
            writeList(child, out, depth + 1);
            first = true; // next leaf atom (if any) starts fresh, not glued to this
        } else {
            if (!first) out << ' ';
            writeAtom(child, out);
            first = false;
        }
    }
    out << ')';
}

} // namespace

std::string writeSExpr(const SExpr& expr) {
    std::ostringstream out;
    if (expr.isList()) {
        writeList(expr, out, 0);
    } else {
        writeAtom(expr, out);
    }
    out << '\n';
    return out.str();
}

} // namespace lcad
