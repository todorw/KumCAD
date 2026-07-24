#include "core/util/Expr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace lcad {

namespace {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;

class Parser {
public:
    explicit Parser(const std::string& s) : m_s(s) {}
    Parser(const std::string& s, const VariableLookup& lookup) : m_s(s), m_lookup(lookup) {}

    std::optional<double> parse() {
        skipWs();
        const double v = parseExpr();
        if (m_failed) return std::nullopt;
        skipWs();
        if (m_pos != m_s.size()) {
            fail("unexpected character '" + std::string(1, m_s[m_pos]) + "'");
            return std::nullopt;
        }
        return v;
    }

    const std::string& error() const { return m_error; }

private:
    const std::string& m_s;
    std::size_t m_pos = 0;
    bool m_failed = false;
    std::string m_error;
    VariableLookup m_lookup;

    void fail(const std::string& msg) {
        if (!m_failed) m_error = msg;
        m_failed = true;
    }

    void skipWs() {
        while (m_pos < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_pos]))) ++m_pos;
    }

    char peek() {
        skipWs();
        return m_pos < m_s.size() ? m_s[m_pos] : '\0';
    }

    bool consume(char c) {
        if (peek() == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    // expr := term (('+' | '-') term)*
    double parseExpr() {
        double v = parseTerm();
        while (!m_failed) {
            const char c = peek();
            if (c == '+') {
                ++m_pos;
                v += parseTerm();
            } else if (c == '-') {
                ++m_pos;
                v -= parseTerm();
            } else {
                break;
            }
        }
        return v;
    }

    // term := unary (('*' | '/') unary)*
    double parseTerm() {
        double v = parseUnary();
        while (!m_failed) {
            const char c = peek();
            if (c == '*') {
                ++m_pos;
                v *= parseUnary();
            } else if (c == '/') {
                ++m_pos;
                const double rhs = parseUnary();
                if (!m_failed && std::abs(rhs) < 1e-15) {
                    fail("division by zero");
                    return 0.0;
                }
                v /= rhs;
            } else {
                break;
            }
        }
        return v;
    }

    // unary := ('-' | '+') unary | power
    double parseUnary() {
        if (consume('-')) return -parseUnary();
        if (consume('+')) return parseUnary();
        return parsePower();
    }

    // power := primary ('^' unary)?  (right-associative)
    double parsePower() {
        const double base = parsePrimary();
        if (!m_failed && consume('^')) {
            const double exponent = parseUnary();
            return std::pow(base, exponent);
        }
        return base;
    }

    double parsePrimary() {
        if (m_failed) return 0.0;
        skipWs();
        if (consume('(')) {
            const double v = parseExpr();
            if (!consume(')')) fail("expected ')'");
            return v;
        }
        const char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return parseNumber();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return parseIdentifier();
        fail(c == '\0' ? "unexpected end of expression" : "unexpected character '" + std::string(1, c) + "'");
        return 0.0;
    }

    double parseNumber() {
        const std::size_t start = m_pos;
        while (m_pos < m_s.size() &&
              (std::isdigit(static_cast<unsigned char>(m_s[m_pos])) || m_s[m_pos] == '.')) {
            ++m_pos;
        }
        if (m_pos < m_s.size() && (m_s[m_pos] == 'e' || m_s[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_s.size() && (m_s[m_pos] == '+' || m_s[m_pos] == '-')) ++m_pos;
            while (m_pos < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_pos]))) ++m_pos;
        }
        try {
            return std::stod(m_s.substr(start, m_pos - start));
        } catch (...) {
            fail("invalid number");
            return 0.0;
        }
    }

    double parseIdentifier() {
        const std::size_t start = m_pos;
        // '.' is allowed inside an identifier (not just alnum/_) so a
        // lookup hook can resolve dotted qualified names like a
        // Spreadsheet cell reference "Sheet1.A1" (see core/document/
        // Spreadsheet.h) -- unambiguous with number parsing, since an
        // identifier only starts here when the FIRST character is
        // alpha/underscore, never a digit or a leading '.' (parseNumber's
        // own entry condition, a completely separate path).
        while (m_pos < m_s.size() && (std::isalnum(static_cast<unsigned char>(m_s[m_pos])) || m_s[m_pos] == '_' ||
                                      m_s[m_pos] == '.')) {
            ++m_pos;
        }
        const std::string original = m_s.substr(start, m_pos - start);
        std::string name = original;
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        if (name == "pi") return M_PI;
        if (name == "e") return M_E;

        if (!consume('(')) {
            // Not a function call: try a variable lookup (case-sensitive,
            // the ORIGINAL text, not the case-folded name above) before
            // failing.
            if (m_lookup) {
                if (auto value = m_lookup(original)) return *value;
            }
            fail("unknown identifier '" + original + "'");
            return 0.0;
        }
        std::vector<double> args;
        if (peek() != ')') {
            args.push_back(parseExpr());
            while (!m_failed && consume(',')) args.push_back(parseExpr());
        }
        if (!consume(')')) fail("expected ')'");
        if (m_failed) return 0.0;
        return callFunction(name, args);
    }

    double callFunction(const std::string& name, const std::vector<double>& args) {
        auto need = [&](std::size_t n) -> bool {
            if (args.size() != n) {
                fail(name + "() takes " + std::to_string(n) + " argument(s)");
                return false;
            }
            return true;
        };
        if (name == "sin") return need(1) ? std::sin(args[0] * kDegToRad) : 0.0;
        if (name == "cos") return need(1) ? std::cos(args[0] * kDegToRad) : 0.0;
        if (name == "tan") return need(1) ? std::tan(args[0] * kDegToRad) : 0.0;
        if (name == "asin") return need(1) ? std::asin(args[0]) * kRadToDeg : 0.0;
        if (name == "acos") return need(1) ? std::acos(args[0]) * kRadToDeg : 0.0;
        if (name == "atan") return need(1) ? std::atan(args[0]) * kRadToDeg : 0.0;
        if (name == "atan2") return need(2) ? std::atan2(args[0], args[1]) * kRadToDeg : 0.0;
        if (name == "sqrt") {
            if (!need(1)) return 0.0;
            if (args[0] < 0.0) {
                fail("sqrt of a negative number");
                return 0.0;
            }
            return std::sqrt(args[0]);
        }
        if (name == "abs") return need(1) ? std::abs(args[0]) : 0.0;
        if (name == "ln") return need(1) ? std::log(args[0]) : 0.0;
        if (name == "log") return need(1) ? std::log10(args[0]) : 0.0;
        if (name == "exp") return need(1) ? std::exp(args[0]) : 0.0;
        if (name == "min") return need(2) ? std::min(args[0], args[1]) : 0.0;
        if (name == "max") return need(2) ? std::max(args[0], args[1]) : 0.0;
        fail("unknown function '" + name + "'");
        return 0.0;
    }
};

} // namespace

std::optional<double> evaluateExpression(const std::string& expr, std::string* errorOut) {
    Parser parser(expr);
    if (auto v = parser.parse()) return v;
    if (errorOut) *errorOut = parser.error();
    return std::nullopt;
}

std::optional<double> evaluateExpression(const std::string& expr, const VariableLookup& lookup,
                                         std::string* errorOut) {
    Parser parser(expr, lookup);
    if (auto v = parser.parse()) return v;
    if (errorOut) *errorOut = parser.error();
    return std::nullopt;
}

} // namespace lcad
