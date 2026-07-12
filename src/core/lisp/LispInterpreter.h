#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lcad {

// A real, but deliberately scoped-down, AutoLISP interpreter: variables
// (setq), control flow (if/cond/while/progn/and/or), user-defined functions
// (defun, with the classic "/ locals" local-variable list), arithmetic,
// list/cons manipulation, strings, and driving KumCAD's own commands via
// (command "LINE" (list 0 0) (list 10 10) "") -- each argument is converted
// to the text AutoCAD's command line would see and fed to whatever sink the
// embedder supplies (CommandDispatcher::handleCommandText in the app).
//
// Not implemented, disclosed: interactive input (getpoint/getreal/getstring/
// getkword -- these suspend AutoCAD's command line waiting for a pick or
// keystroke, which this synchronous embedding doesn't support), entity/
// selection-set access (entget/ssget and friends), getvar/setvar, and DCL
// dialogs. Scripts drive existing commands with literal or computed
// arguments rather than prompting the user interactively.
class LispInterpreter {
public:
    enum class Kind { Nil, True, Number, String, Symbol, Cons };

    struct ConsCell;
    // A single AutoLISP value. Lists are singly-linked cons cells
    // (shared_ptr so structure can be shared the way real Lisp lists are).
    struct Value {
        Kind kind = Kind::Nil;
        double number = 0.0;
        std::string text; // string content, or the symbol name
        std::shared_ptr<ConsCell> cell;

        static Value nil() { return Value{}; }
        static Value t() {
            Value v;
            v.kind = Kind::True;
            return v;
        }
        static Value num(double d) {
            Value v;
            v.kind = Kind::Number;
            v.number = d;
            return v;
        }
        static Value str(std::string s) {
            Value v;
            v.kind = Kind::String;
            v.text = std::move(s);
            return v;
        }
        static Value sym(std::string s) {
            Value v;
            v.kind = Kind::Symbol;
            v.text = std::move(s);
            return v;
        }
        bool isNil() const { return kind == Kind::Nil; }
        // Everything except nil is "true" in AutoLISP conditionals.
        bool truthy() const { return kind != Kind::Nil; }
    };

    struct ConsCell {
        Value car;
        Value cdr;
    };

    struct LambdaDef {
        std::vector<std::string> params;
        std::vector<std::string> locals;
        std::vector<Value> body; // each element is an unevaluated form
    };

    // A scope chain: every function call gets a fresh scope parented to the
    // global scope (not full lexical closures -- matches classic AutoLISP
    // well enough for scripts that don't rely on nested-function capture).
    class Env {
    public:
        explicit Env(Env* parent = nullptr) : m_parent(parent) {}
        const Value* find(const std::string& name) const;
        void define(const std::string& name, Value v) { m_vars[name] = std::move(v); }
        // Updates the nearest scope that already has `name`; if none does,
        // defines it in the outermost (global) scope, matching AutoLISP's
        // setq-creates-a-global-if-unbound behavior.
        void set(const std::string& name, Value v);

    private:
        Env* m_parent;
        std::unordered_map<std::string, Value> m_vars;
    };

    // commandSink receives each (command ...) argument as text, in order,
    // exactly as if it had been typed at the command line.
    explicit LispInterpreter(std::function<void(const std::string&)> commandSink);

    struct RunResult {
        bool ok = false;
        std::string output;     // princ/prin1/print/terpri output, concatenated
        std::string resultText; // printed representation of the last top-level form
        std::string error;
    };

    // Parses and evaluates every top-level form in source, in order, against
    // this interpreter's persistent global environment (so defuns/setqs from
    // an earlier call are visible to a later one, like a real AutoLISP
    // session). Stops at the first error.
    RunResult run(const std::string& source);

private:
    Env m_global;
    std::unordered_map<std::string, std::shared_ptr<LambdaDef>> m_functions;
    std::function<void(const std::string&)> m_commandSink;
    std::string m_output;

    Value eval(const Value& form, Env& env);
    Value evalBody(const std::vector<Value>& body, Env& env);
    Value callLambda(const LambdaDef& fn, std::vector<Value>& args);
    Value callBuiltin(const std::string& name, std::vector<Value>& args, bool& handled);
    std::string printValue(const Value& v, bool quoteStrings) const;
    std::string toCommandText(const Value& v) const;
};

} // namespace lcad
