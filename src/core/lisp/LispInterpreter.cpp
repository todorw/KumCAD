#include "core/lisp/LispInterpreter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace lcad {

using Value = LispInterpreter::Value;
using Kind = LispInterpreter::Kind;
using ConsCell = LispInterpreter::ConsCell;
using LambdaDef = LispInterpreter::LambdaDef;

namespace {

struct LispError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::vector<Value> vectorFromList(const Value& v) {
    std::vector<Value> out;
    Value cur = v;
    while (cur.kind == Kind::Cons) {
        out.push_back(cur.cell->car);
        cur = cur.cell->cdr;
    }
    return out;
}

Value listFromVector(const std::vector<Value>& items) {
    Value result = Value::nil();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        auto cell = std::make_shared<ConsCell>();
        cell->car = *it;
        cell->cdr = result;
        Value v;
        v.kind = Kind::Cons;
        v.cell = cell;
        result = v;
    }
    return result;
}

Value cons(const Value& car, const Value& cdr) {
    auto cell = std::make_shared<ConsCell>();
    cell->car = car;
    cell->cdr = cdr;
    Value v;
    v.kind = Kind::Cons;
    v.cell = cell;
    return v;
}

bool valuesEqual(const Value& a, const Value& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case Kind::Nil:
    case Kind::True:
        return true;
    case Kind::Number:
        return a.number == b.number;
    case Kind::String:
    case Kind::Symbol:
        return a.text == b.text;
    case Kind::Cons:
        return a.cell == b.cell;
    }
    return false;
}

std::string formatNumber(double d) {
    if (std::abs(d - std::llround(d)) < 1e-9 && std::abs(d) < 1e15) return std::to_string(std::llround(d));
    std::ostringstream oss;
    oss << d;
    return oss.str();
}

bool isDelimiter(char c) {
    return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '"' || c == ';' || c == '\'';
}

// Reads one AutoLISP source string into a sequence of top-level forms.
// Symbols are canonicalized to uppercase (case-insensitive, like real
// AutoLISP); the sole reader macro is ' for (quote ...).
class Reader {
public:
    explicit Reader(const std::string& src) : m_s(src) {}

    bool readForm(Value& out) {
        skipWs();
        if (atEnd()) return false;
        out = readValue();
        return true;
    }

private:
    const std::string& m_s;
    std::size_t m_pos = 0;

    bool atEnd() const { return m_pos >= m_s.size(); }
    char peek() const { return m_pos < m_s.size() ? m_s[m_pos] : '\0'; }
    char advance() { return m_s[m_pos++]; }

    void skipWs() {
        for (;;) {
            while (m_pos < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_pos]))) ++m_pos;
            if (m_pos < m_s.size() && m_s[m_pos] == ';') {
                while (m_pos < m_s.size() && m_s[m_pos] != '\n') ++m_pos;
                continue;
            }
            break;
        }
    }

    Value readValue() {
        skipWs();
        if (atEnd()) throw LispError("unexpected end of input");
        const char c = peek();
        if (c == '(') {
            advance();
            return readList();
        }
        if (c == ')') throw LispError("unexpected ')'");
        if (c == '\'') {
            advance();
            return cons(Value::sym("QUOTE"), cons(readValue(), Value::nil()));
        }
        if (c == '"') return readString();
        return readToken();
    }

    Value readList() {
        std::vector<Value> items;
        skipWs();
        while (!atEnd() && peek() != ')') {
            items.push_back(readValue());
            skipWs();
        }
        if (atEnd()) throw LispError("unmatched '('");
        advance(); // ')'
        return listFromVector(items);
    }

    Value readString() {
        advance(); // opening quote
        std::string text;
        while (!atEnd() && peek() != '"') {
            char c = advance();
            if (c == '\\' && !atEnd()) {
                const char esc = advance();
                switch (esc) {
                case 'n':
                    text += '\n';
                    break;
                case 't':
                    text += '\t';
                    break;
                case '"':
                    text += '"';
                    break;
                case '\\':
                    text += '\\';
                    break;
                default:
                    text += esc;
                    break;
                }
            } else {
                text += c;
            }
        }
        if (atEnd()) throw LispError("unterminated string literal");
        advance(); // closing quote
        return Value::str(text);
    }

    Value readToken() {
        const std::size_t start = m_pos;
        while (!atEnd() && !isDelimiter(m_s[m_pos])) ++m_pos;
        const std::string tok = m_s.substr(start, m_pos - start);
        if (tok.empty()) throw LispError("unexpected character");

        char* end = nullptr;
        const double d = std::strtod(tok.c_str(), &end);
        if (end == tok.c_str() + tok.size()) return Value::num(d);

        std::string upper = tok;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (upper == "NIL") return Value::nil();
        if (upper == "T") return Value::t();
        return Value::sym(upper);
    }
};

} // namespace

const Value* LispInterpreter::Env::find(const std::string& name) const {
    auto it = m_vars.find(name);
    if (it != m_vars.end()) return &it->second;
    return m_parent ? m_parent->find(name) : nullptr;
}

void LispInterpreter::Env::set(const std::string& name, Value v) {
    Env* e = this;
    for (;;) {
        auto it = e->m_vars.find(name);
        if (it != e->m_vars.end()) {
            it->second = std::move(v);
            return;
        }
        if (!e->m_parent) {
            e->m_vars[name] = std::move(v); // undeclared: falls back to the global scope
            return;
        }
        e = e->m_parent;
    }
}

LispInterpreter::LispInterpreter(std::function<void(const std::string&)> commandSink)
    : m_commandSink(std::move(commandSink)) {}

LispInterpreter::RunResult LispInterpreter::run(const std::string& source) {
    RunResult result;
    m_output.clear();
    Reader reader(source);
    Value form;
    Value last = Value::nil();
    try {
        while (reader.readForm(form)) last = eval(form, m_global);
        result.ok = true;
        result.resultText = printValue(last, true);
    } catch (const LispError& e) {
        result.ok = false;
        result.error = e.what();
    } catch (const std::exception& e) {
        result.ok = false;
        result.error = e.what();
    }
    result.output = m_output;
    return result;
}

Value LispInterpreter::evalBody(const std::vector<Value>& body, Env& env) {
    Value result;
    for (const Value& form : body) result = eval(form, env);
    return result;
}

Value LispInterpreter::callLambda(const LambdaDef& fn, std::vector<Value>& args) {
    if (args.size() != fn.params.size()) {
        throw LispError("function expects " + std::to_string(fn.params.size()) + " argument(s), got " +
                        std::to_string(args.size()));
    }
    Env local(&m_global);
    for (std::size_t i = 0; i < fn.params.size(); ++i) local.define(fn.params[i], args[i]);
    for (const std::string& name : fn.locals) local.define(name, Value::nil());
    return evalBody(fn.body, local);
}

Value LispInterpreter::eval(const Value& form, Env& env) {
    switch (form.kind) {
    case Kind::Nil:
    case Kind::True:
    case Kind::Number:
    case Kind::String:
        return form;
    case Kind::Symbol: {
        const Value* v = env.find(form.text);
        if (!v) throw LispError("unbound variable: " + form.text);
        return *v;
    }
    case Kind::Cons:
        break;
    }

    const Value& opVal = form.cell->car;
    if (opVal.kind != Kind::Symbol) throw LispError("invalid function call (not a symbol)");
    const std::string& op = opVal.text;

    std::vector<Value> rawArgs;
    for (Value rest = form.cell->cdr; rest.kind == Kind::Cons; rest = rest.cell->cdr) {
        rawArgs.push_back(rest.cell->car);
    }

    // Special forms: these control which of their arguments get evaluated
    // (or evaluate them repeatedly/conditionally), so they can't go through
    // the uniform "evaluate every argument" path below.
    if (op == "QUOTE") {
        if (rawArgs.size() != 1) throw LispError("quote takes 1 argument");
        return rawArgs[0];
    }
    if (op == "SETQ") {
        if (rawArgs.empty() || rawArgs.size() % 2 != 0) throw LispError("setq needs name/value pairs");
        Value result;
        for (std::size_t i = 0; i < rawArgs.size(); i += 2) {
            if (rawArgs[i].kind != Kind::Symbol) throw LispError("setq: expected a symbol");
            result = eval(rawArgs[i + 1], env);
            env.set(rawArgs[i].text, result);
        }
        return result;
    }
    if (op == "IF") {
        if (rawArgs.size() < 2 || rawArgs.size() > 3) throw LispError("if takes 2 or 3 arguments");
        if (eval(rawArgs[0], env).truthy()) return eval(rawArgs[1], env);
        return rawArgs.size() == 3 ? eval(rawArgs[2], env) : Value::nil();
    }
    if (op == "PROGN") return evalBody(rawArgs, env);
    if (op == "WHILE") {
        if (rawArgs.empty()) throw LispError("while needs a test expression");
        Value result;
        while (eval(rawArgs[0], env).truthy()) {
            for (std::size_t i = 1; i < rawArgs.size(); ++i) result = eval(rawArgs[i], env);
        }
        return result;
    }
    if (op == "AND") {
        Value result = Value::t();
        for (const Value& a : rawArgs) {
            result = eval(a, env);
            if (!result.truthy()) return Value::nil();
        }
        return result;
    }
    if (op == "OR") {
        for (const Value& a : rawArgs) {
            Value v = eval(a, env);
            if (v.truthy()) return v;
        }
        return Value::nil();
    }
    if (op == "COND") {
        for (const Value& clause : rawArgs) {
            const std::vector<Value> parts = vectorFromList(clause);
            if (parts.empty()) continue;
            const Value test = eval(parts[0], env);
            if (test.truthy()) {
                Value result = test;
                for (std::size_t i = 1; i < parts.size(); ++i) result = eval(parts[i], env);
                return result;
            }
        }
        return Value::nil();
    }
    if (op == "DEFUN") {
        if (rawArgs.size() < 2 || rawArgs[0].kind != Kind::Symbol) {
            throw LispError("defun: expected a name and a parameter list");
        }
        auto def = std::make_shared<LambdaDef>();
        bool inLocals = false;
        for (const Value& p : vectorFromList(rawArgs[1])) {
            if (p.kind == Kind::Symbol && p.text == "/") {
                inLocals = true;
                continue;
            }
            if (p.kind != Kind::Symbol) throw LispError("defun: parameter list must be symbols");
            (inLocals ? def->locals : def->params).push_back(p.text);
        }
        for (std::size_t i = 2; i < rawArgs.size(); ++i) def->body.push_back(rawArgs[i]);
        m_functions[rawArgs[0].text] = def;
        return Value::sym(rawArgs[0].text);
    }

    // Ordinary call: evaluate every argument, then dispatch to a
    // user-defined function or a builtin.
    std::vector<Value> args;
    args.reserve(rawArgs.size());
    for (const Value& a : rawArgs) args.push_back(eval(a, env));

    if (auto it = m_functions.find(op); it != m_functions.end()) return callLambda(*it->second, args);

    bool handled = false;
    Value result = callBuiltin(op, args, handled);
    if (!handled) throw LispError("unknown function: " + op);
    return result;
}

Value LispInterpreter::callBuiltin(const std::string& name, std::vector<Value>& a, bool& handled) {
    handled = true;
    auto num = [&](const Value& v) -> double {
        if (v.kind != Kind::Number) throw LispError(name + ": expected a number");
        return v.number;
    };
    auto str = [&](const Value& v) -> const std::string& {
        if (v.kind != Kind::String) throw LispError(name + ": expected a string");
        return v.text;
    };

    if (name == "+") {
        double r = 0.0;
        for (const Value& v : a) r += num(v);
        return Value::num(r);
    }
    if (name == "*") {
        double r = 1.0;
        for (const Value& v : a) r *= num(v);
        return Value::num(r);
    }
    if (name == "-") {
        if (a.empty()) throw LispError("-: needs at least 1 argument");
        if (a.size() == 1) return Value::num(-num(a[0]));
        double r = num(a[0]);
        for (std::size_t i = 1; i < a.size(); ++i) r -= num(a[i]);
        return Value::num(r);
    }
    if (name == "/") {
        if (a.empty()) throw LispError("/: needs at least 1 argument");
        double r = a.size() == 1 ? 1.0 : num(a[0]);
        for (std::size_t i = (a.size() == 1 ? 0 : 1); i < a.size(); ++i) {
            const double d = num(a[i]);
            if (std::abs(d) < 1e-15) throw LispError("/: division by zero");
            r /= d;
        }
        return Value::num(r);
    }
    if (name == "1+") {
        if (a.size() != 1) throw LispError("1+: needs 1 argument");
        return Value::num(num(a[0]) + 1.0);
    }
    if (name == "1-") {
        if (a.size() != 1) throw LispError("1-: needs 1 argument");
        return Value::num(num(a[0]) - 1.0);
    }
    if (name == "=" || name == "/=") {
        if (a.size() < 2) throw LispError(name + ": needs at least 2 arguments");
        const bool eq = valuesEqual(a[0], a[1]);
        return (name == "=" ? eq : !eq) ? Value::t() : Value::nil();
    }
    if (name == "<" || name == ">" || name == "<=" || name == ">=") {
        if (a.size() < 2) throw LispError(name + ": needs at least 2 arguments");
        for (std::size_t i = 0; i + 1 < a.size(); ++i) {
            const double x = num(a[i]);
            const double y = num(a[i + 1]);
            const bool ok = name == "<" ? x < y : name == ">" ? x > y : name == "<=" ? x <= y : x >= y;
            if (!ok) return Value::nil();
        }
        return Value::t();
    }

    if (name == "CAR" || name == "CDR") {
        if (a.size() != 1) throw LispError(name + ": needs 1 argument");
        if (a[0].kind != Kind::Cons) return Value::nil();
        return name == "CAR" ? a[0].cell->car : a[0].cell->cdr;
    }
    if (name == "CAAR" || name == "CADR" || name == "CDAR" || name == "CDDR") {
        if (a.size() != 1) throw LispError(name + ": needs 1 argument");
        auto step = [](const Value& v, bool wantCar) -> Value {
            return v.kind == Kind::Cons ? (wantCar ? v.cell->car : v.cell->cdr) : Value::nil();
        };
        // "cXYr" applies Y (the letter nearer 'r', i.e. name[2]) first/innermost,
        // then X (nearer 'c', name[1]) last/outermost -- cadr(x) = car(cdr(x)).
        const Value inner = step(a[0], name[2] == 'A');
        return step(inner, name[1] == 'A');
    }
    if (name == "CONS") {
        if (a.size() != 2) throw LispError("cons: needs 2 arguments");
        return cons(a[0], a[1]);
    }
    if (name == "LIST") return listFromVector(a);
    if (name == "NTH") {
        if (a.size() != 2) throw LispError("nth: needs 2 arguments");
        int idx = static_cast<int>(num(a[0]));
        Value cur = a[1];
        while (idx > 0 && cur.kind == Kind::Cons) {
            cur = cur.cell->cdr;
            --idx;
        }
        return cur.kind == Kind::Cons ? cur.cell->car : Value::nil();
    }
    if (name == "LENGTH") {
        if (a.size() != 1) throw LispError("length: needs 1 argument");
        int n = 0;
        for (Value cur = a[0]; cur.kind == Kind::Cons; cur = cur.cell->cdr) ++n;
        return Value::num(n);
    }
    if (name == "APPEND") {
        std::vector<Value> all;
        for (const Value& lst : a) {
            const auto items = vectorFromList(lst);
            all.insert(all.end(), items.begin(), items.end());
        }
        return listFromVector(all);
    }
    if (name == "REVERSE") {
        if (a.size() != 1) throw LispError("reverse: needs 1 argument");
        auto items = vectorFromList(a[0]);
        std::reverse(items.begin(), items.end());
        return listFromVector(items);
    }
    if (name == "LAST") {
        if (a.size() != 1) throw LispError("last: needs 1 argument");
        const auto items = vectorFromList(a[0]);
        return items.empty() ? Value::nil() : items.back();
    }
    if (name == "MEMBER") {
        if (a.size() != 2) throw LispError("member: needs 2 arguments");
        for (Value cur = a[1]; cur.kind == Kind::Cons; cur = cur.cell->cdr) {
            if (valuesEqual(cur.cell->car, a[0])) return cur;
        }
        return Value::nil();
    }

    if (name == "ATOM") return (a.size() == 1 && a[0].kind != Kind::Cons) ? Value::t() : Value::nil();
    if (name == "LISTP") {
        return (a.size() == 1 && (a[0].kind == Kind::Cons || a[0].kind == Kind::Nil)) ? Value::t() : Value::nil();
    }
    if (name == "NUMBERP") return (a.size() == 1 && a[0].kind == Kind::Number) ? Value::t() : Value::nil();
    if (name == "STRINGP") return (a.size() == 1 && a[0].kind == Kind::String) ? Value::t() : Value::nil();
    if (name == "NULL" || name == "NOT") {
        if (a.size() != 1) throw LispError(name + ": needs 1 argument");
        return a[0].truthy() ? Value::nil() : Value::t();
    }
    if (name == "ZEROP") {
        if (a.size() != 1) throw LispError("zerop: needs 1 argument");
        return num(a[0]) == 0.0 ? Value::t() : Value::nil();
    }
    if (name == "MINUSP") {
        if (a.size() != 1) throw LispError("minusp: needs 1 argument");
        return num(a[0]) < 0.0 ? Value::t() : Value::nil();
    }

    if (name == "STRCAT") {
        std::string r;
        for (const Value& v : a) r += str(v);
        return Value::str(r);
    }
    if (name == "STRLEN") {
        if (a.size() != 1) throw LispError("strlen: needs 1 argument");
        return Value::num(static_cast<double>(str(a[0]).size()));
    }
    if (name == "SUBSTR") {
        if (a.size() < 2 || a.size() > 3) throw LispError("substr: needs 2 or 3 arguments");
        const std::string& s = str(a[0]);
        long start = static_cast<long>(num(a[1]));
        if (start < 1) start = 1;
        if (static_cast<std::size_t>(start) > s.size()) return Value::str("");
        const std::size_t begin = static_cast<std::size_t>(start - 1);
        const std::size_t count = a.size() == 3 ? static_cast<std::size_t>(std::max(0.0, num(a[2])))
                                                : s.size() - begin;
        return Value::str(s.substr(begin, count));
    }
    if (name == "STRCASE") {
        if (a.empty()) throw LispError("strcase: needs at least 1 argument");
        const bool toLower = a.size() > 1 && a[1].truthy();
        std::string r = str(a[0]);
        std::transform(r.begin(), r.end(), r.begin(), [toLower](unsigned char c) {
            return static_cast<char>(toLower ? std::tolower(c) : std::toupper(c));
        });
        return Value::str(r);
    }
    if (name == "ITOA") {
        if (a.size() != 1) throw LispError("itoa: needs 1 argument");
        return Value::str(std::to_string(static_cast<long long>(num(a[0]))));
    }
    if (name == "ATOI") {
        if (a.size() != 1) throw LispError("atoi: needs 1 argument");
        return Value::num(std::atoi(str(a[0]).c_str()));
    }
    if (name == "ATOF") {
        if (a.size() != 1) throw LispError("atof: needs 1 argument");
        return Value::num(std::atof(str(a[0]).c_str()));
    }
    if (name == "RTOS") {
        if (a.empty()) throw LispError("rtos: needs at least 1 argument");
        return Value::str(formatNumber(num(a[0])));
    }

    if (name == "PRINC" || name == "PRIN1" || name == "PRINT") {
        if (a.size() > 1) throw LispError(name + ": too many arguments");
        const std::string text = a.empty() ? "" : printValue(a[0], name != "PRINC");
        if (name == "PRINT") m_output += "\n" + text + " ";
        else m_output += text;
        return a.empty() ? Value::nil() : a[0];
    }
    if (name == "TERPRI") {
        m_output += "\n";
        return Value::nil();
    }
    if (name == "COMMAND") {
        for (const Value& v : a) {
            if (m_commandSink) m_commandSink(toCommandText(v));
        }
        return Value::nil();
    }

    handled = false;
    return Value::nil();
}

std::string LispInterpreter::printValue(const Value& v, bool quoteStrings) const {
    switch (v.kind) {
    case Kind::Nil:
        return "nil";
    case Kind::True:
        return "T";
    case Kind::Number:
        return formatNumber(v.number);
    case Kind::String:
        return quoteStrings ? ("\"" + v.text + "\"") : v.text;
    case Kind::Symbol:
        return v.text;
    case Kind::Cons: {
        std::string s = "(";
        bool first = true;
        for (Value cur = v; cur.kind == Kind::Cons; cur = cur.cell->cdr) {
            if (!first) s += " ";
            s += printValue(cur.cell->car, true);
            first = false;
        }
        s += ")";
        return s;
    }
    }
    return "";
}

std::string LispInterpreter::toCommandText(const Value& v) const {
    if (v.kind == Kind::String) return v.text;
    if (v.kind == Kind::Number) return formatNumber(v.number);
    if (v.kind == Kind::Cons) {
        const auto items = vectorFromList(v);
        if (items.size() >= 2 && items[0].kind == Kind::Number && items[1].kind == Kind::Number) {
            return formatNumber(items[0].number) + "," + formatNumber(items[1].number);
        }
    }
    return printValue(v, false);
}

} // namespace lcad
