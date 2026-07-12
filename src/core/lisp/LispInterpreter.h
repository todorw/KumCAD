#pragma once

#include "core/Ids.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lcad {

class Document;

// A real, but deliberately scoped-down, AutoLISP interpreter: variables
// (setq), control flow (if/cond/while/progn/and/or), user-defined functions
// (defun, with the classic "/ locals" local-variable list), arithmetic,
// list/cons manipulation, strings, and driving KumCAD's own commands via
// (command "LINE" (list 0 0) (list 10 10) "") -- each argument is converted
// to the text AutoCAD's command line would see and fed to whatever sink the
// embedder supplies (CommandDispatcher::handleCommandText in the app).
// command is an ordinary function here, not a special form, so all of its
// arguments are evaluated before any of them are sent: prefer
// (setq p1 (getpoint)) (command "LINE" p1 ...) over inlining (getpoint)
// directly as a command argument, which would prompt before "LINE" itself
// had been sent (real AutoLISP interleaves the two).
//
// When constructed with a Document, also supports getvar/setvar (CLAYER is
// wired to the document's current layer; any other name is a plain in-memory
// variable, not a real AutoCAD system variable), entget (a DXF-group-code
// association list, covering LINE/CIRCLE/ARC/TEXT/POINT/LWPOLYLINE/INSERT --
// other types return just their (0 "type") and (8 "layer") entries), and
// ssget ("X" selects everything, optionally filtered by a DXF-style
// ((0 "LINE") (8 "layer")) list -- proper 2-element lists rather than real
// AutoLISP's dotted (0 . "LINE") pairs, since this reader has no dotted-pair
// syntax; the interactive point-picking ssget forms aren't supported). A
// selection set is just a plain list of entity names (see below), so
// sslength/ssname/ssadd/ssdel work on it via ordinary list operations. An
// "entity name" is simply its EntityId as a Lisp number, not an opaque
// handle like real AutoLISP's ename type.
//
// When constructed with interactiveInput, also supports getpoint/getreal/
// getstring/getkword (getdist and getint are aliased to getreal -- no
// rubber-band line for getdist, no integer truncation for getint). Not
// implemented, disclosed: DCL dialogs, and initget (getkword takes its
// keyword list as an explicit second argument instead, since there's no
// initget to source it from).
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
    //
    // interactiveInput, if given, backs getpoint/getreal/getstring/getkword:
    // called with a prompt, (getkword only) the valid keyword list, and
    // whether this is a getpoint call (so the embedder knows a click should
    // resolve it too, not just typed text), it must block until the user
    // responds and return the raw typed text (or the "x,y" text a click
    // resolves to), or nullopt if they cancelled (Escape). The embedder is
    // expected to implement this with a nested Qt event loop (see
    // CommandDispatcher::waitForLispInput) so the UI stays responsive while
    // a script is paused waiting for input -- LispInterpreter itself stays
    // free of any Qt dependency.
    using InteractiveInputSink = std::function<std::optional<std::string>(
        const std::string& prompt, const std::vector<std::string>& keywords, bool isPoint)>;
    explicit LispInterpreter(std::function<void(const std::string&)> commandSink, Document* document = nullptr,
                             InteractiveInputSink interactiveInput = nullptr);

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
    Document* m_document;
    InteractiveInputSink m_interactiveInput;
    std::unordered_map<std::string, Value> m_sysvars; // getvar/setvar, for names not backed by real state
    std::string m_output;

    Value builtinGetvar(std::vector<Value>& args);
    Value builtinSetvar(std::vector<Value>& args);
    Value builtinEntget(std::vector<Value>& args);
    Value builtinSsget(std::vector<Value>& args);
    Value entityToAssocList(EntityId id) const;
    Value builtinGetPoint(std::vector<Value>& args);
    Value builtinGetReal(std::vector<Value>& args);
    Value builtinGetString(std::vector<Value>& args);
    Value builtinGetKword(std::vector<Value>& args);

    Value eval(const Value& form, Env& env);
    Value evalBody(const std::vector<Value>& body, Env& env);
    Value callLambda(const LambdaDef& fn, std::vector<Value>& args);
    Value callBuiltin(const std::string& name, std::vector<Value>& args, bool& handled);
    std::string printValue(const Value& v, bool quoteStrings) const;
    std::string toCommandText(const Value& v) const;
};

} // namespace lcad
