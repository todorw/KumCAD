#include "core/lisp/LispInterpreter.h"

#include "core/document/Document.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"

#include <catch2/catch_test_macros.hpp>

namespace {

lcad::LispInterpreter::RunResult run(const std::string& src) {
    lcad::LispInterpreter interp([](const std::string&) {});
    return interp.run(src);
}

} // namespace

TEST_CASE("LispInterpreter evaluates arithmetic and comparisons", "[lisp]") {
    REQUIRE(run("(+ 1 2 3)").resultText == "6");
    REQUIRE(run("(- 10 3 2)").resultText == "5");
    REQUIRE(run("(* 2 3 4)").resultText == "24");
    REQUIRE(run("(/ 20 2 5)").resultText == "2");
    REQUIRE(run("(- 5)").resultText == "-5");
    REQUIRE(run("(1+ 5)").resultText == "6");
    REQUIRE(run("(1- 5)").resultText == "4");
    REQUIRE(run("(< 1 2 3)").resultText == "T");
    REQUIRE(run("(< 1 3 2)").resultText == "nil");
    REQUIRE(run("(= 3 3)").resultText == "T");
    REQUIRE(run("(/= 3 4)").resultText == "T");
}

TEST_CASE("LispInterpreter handles variables, if, and progn", "[lisp]") {
    REQUIRE(run("(setq x 5) (+ x 1)").resultText == "6");
    REQUIRE(run("(if (> 3 2) \"yes\" \"no\")").resultText == "\"yes\"");
    REQUIRE(run("(if (< 3 2) \"yes\" \"no\")").resultText == "\"no\"");
    REQUIRE(run("(progn (setq a 1) (setq a (+ a 1)) a)").resultText == "2");
}

TEST_CASE("LispInterpreter handles while loops", "[lisp]") {
    const auto r = run("(setq i 0) (setq total 0) (while (< i 5) (setq total (+ total i)) (setq i (1+ i))) total");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "10"); // 0+1+2+3+4
}

TEST_CASE("LispInterpreter handles cond, and, or, not", "[lisp]") {
    REQUIRE(run("(cond ((= 1 2) \"a\") ((= 1 1) \"b\") (T \"c\"))").resultText == "\"b\"");
    REQUIRE(run("(and 1 2 3)").resultText == "3");
    REQUIRE(run("(and 1 nil 3)").resultText == "nil");
    REQUIRE(run("(or nil nil 5)").resultText == "5");
    REQUIRE(run("(not nil)").resultText == "T");
    REQUIRE(run("(not 5)").resultText == "nil");
}

TEST_CASE("LispInterpreter supports defun with locals and recursion", "[lisp]") {
    REQUIRE(run("(defun square (x) (* x x)) (square 7)").resultText == "49");
    REQUIRE(run("(defun add3 (a b c / s) (setq s (+ a b c)) s) (add3 1 2 3)").resultText == "6");

    const auto factorial = run(
        "(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n))))) (fact 6)");
    REQUIRE(factorial.ok);
    REQUIRE(factorial.resultText == "720");
}

TEST_CASE("LispInterpreter handles lists and cons cells", "[lisp]") {
    REQUIRE(run("(car (list 1 2 3))").resultText == "1");
    REQUIRE(run("(cdr (list 1 2 3))").resultText == "(2 3)");
    REQUIRE(run("(cadr (list 1 2 3))").resultText == "2");
    REQUIRE(run("(cons 1 (list 2 3))").resultText == "(1 2 3)");
    REQUIRE(run("(length (list 1 2 3 4))").resultText == "4");
    REQUIRE(run("(reverse (list 1 2 3))").resultText == "(3 2 1)");
    REQUIRE(run("(append (list 1 2) (list 3 4))").resultText == "(1 2 3 4)");
    REQUIRE(run("(nth 1 (list \"a\" \"b\" \"c\"))").resultText == "\"b\"");
    REQUIRE(run("(atom 5)").resultText == "T");
    REQUIRE(run("(atom (list 1 2))").resultText == "nil");
    REQUIRE(run("(listp (list 1))").resultText == "T");
    REQUIRE(run("'(1 2 3)").resultText == "(1 2 3)");
}

TEST_CASE("LispInterpreter handles strings", "[lisp]") {
    REQUIRE(run("(strcat \"foo\" \"bar\")").resultText == "\"foobar\"");
    REQUIRE(run("(strlen \"hello\")").resultText == "5");
    REQUIRE(run("(substr \"hello world\" 7)").resultText == "\"world\"");
    REQUIRE(run("(substr \"hello world\" 1 5)").resultText == "\"hello\"");
    REQUIRE(run("(strcase \"Hello\")").resultText == "\"HELLO\"");
    REQUIRE(run("(itoa 42)").resultText == "\"42\"");
    REQUIRE(run("(atoi \"42\")").resultText == "42");
}

TEST_CASE("LispInterpreter's princ/print output is separate from the result", "[lisp]") {
    const auto r = run("(princ \"hi \") (princ \"there\")");
    REQUIRE(r.ok);
    REQUIRE(r.output == "hi there");
}

TEST_CASE("LispInterpreter's (command ...) drives the embedder's sink as text", "[lisp]") {
    std::vector<std::string> sink;
    lcad::LispInterpreter interp([&sink](const std::string& s) { sink.push_back(s); });
    const auto r = interp.run("(command \"LINE\" (list 0 0) (list 10 10) \"\")");
    REQUIRE(r.ok);
    REQUIRE(sink == std::vector<std::string>{"LINE", "0,0", "10,10", ""});
}

TEST_CASE("LispInterpreter's getvar/setvar reads and writes document state", "[lisp]") {
    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{200, 50, 50});
    doc.setCurrentLayer(wallsLayer);

    lcad::LispInterpreter interp([](const std::string&) {}, &doc);
    auto r = interp.run("(getvar \"CLAYER\")");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "\"Walls\"");

    r = interp.run("(setvar \"CLAYER\" \"0\")");
    REQUIRE(r.ok);
    REQUIRE(doc.currentLayer() == 0);

    // Unknown names are a plain in-memory store, not a real sysvar.
    r = interp.run("(setvar \"MYVAR\" 42) (getvar \"MYVAR\")");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "42");
}

TEST_CASE("LispInterpreter's entget returns a DXF-style association list", "[lisp]") {
    lcad::Document doc;
    const lcad::EntityId lineId =
        doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(lineId, doc.currentLayer(), lcad::Point2D(1, 2),
                                                      lcad::Point2D(3, 4)));

    lcad::LispInterpreter interp([](const std::string&) {}, &doc);
    const auto r = interp.run("(setq e (entget " + std::to_string(lineId) +
                              ")) (assoc 0 e)");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "(0 \"LINE\")");

    const auto r2 = interp.run("(assoc 10 e)");
    REQUIRE(r2.ok);
    REQUIRE(r2.resultText == "(10 1 2)");
}

TEST_CASE("LispInterpreter's ssget selects and filters entities", "[lisp]") {
    lcad::Document doc;
    doc.addEntity(
        std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                           lcad::Point2D(1, 1)));
    doc.addEntity(std::make_unique<lcad::CircleEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                        lcad::Point2D(0, 0), 5.0));

    lcad::LispInterpreter interp([](const std::string&) {}, &doc);
    auto r = interp.run("(sslength (ssget \"X\"))");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "2");

    r = interp.run("(sslength (ssget \"X\" (list (list 0 \"CIRCLE\"))))");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "1");
}

TEST_CASE("LispInterpreter's interactive input builtins call the embedder's sink", "[lisp]") {
    std::vector<std::string> prompts;
    lcad::LispInterpreter interp(
        [](const std::string&) {}, nullptr,
        [&prompts](const std::string& prompt, const std::vector<std::string>& keywords,
                   bool /*isPoint*/) -> std::optional<std::string> {
            prompts.push_back(prompt);
            if (prompt == "Pick a point: ") return "3,4";
            if (prompt == "Enter radius: ") return "2.5";
            if (prompt == "Enter name: ") return "hello world";
            if (!keywords.empty()) return keywords.front();
            return std::nullopt;
        });

    auto r = interp.run("(getpoint \"Pick a point: \")");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "(3 4)");

    r = interp.run("(getreal \"Enter radius: \")");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "2.5");

    r = interp.run("(getstring \"Enter name: \")");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "\"hello world\"");

    r = interp.run("(getkword \"Yes or no? \" (list \"Yes\" \"No\"))");
    REQUIRE(r.ok);
    REQUIRE(r.resultText == "\"Yes\"");

    REQUIRE(prompts.size() == 4);
}

TEST_CASE("LispInterpreter's interactive input returns nil without an embedder sink or on cancel", "[lisp]") {
    // No sink at all (e.g. a headless run): getpoint/getreal/getstring/
    // getkword degrade to nil instead of crashing.
    REQUIRE(run("(getpoint)").resultText == "nil");
    REQUIRE(run("(getreal)").resultText == "nil");
    REQUIRE(run("(getstring)").resultText == "nil");
    REQUIRE(run("(getkword \"pick\" (list \"A\" \"B\"))").resultText == "nil");

    // A sink that reports cancellation (nullopt) also degrades to nil.
    lcad::LispInterpreter cancelling([](const std::string&) {}, nullptr,
                                     [](const std::string&, const std::vector<std::string>&, bool) { return std::nullopt; });
    REQUIRE(cancelling.run("(getpoint)").resultText == "nil");
}

TEST_CASE("LispInterpreter's getpoint result feeds into (command ...) via setq", "[lisp]") {
    // The documented idiom (LispInterpreter.h): resolve points with setq
    // first, then pass them to command, since command isn't a special form
    // and would otherwise prompt before "LINE" itself had been sent.
    std::vector<std::string> sink;
    lcad::LispInterpreter interp(
        [&sink](const std::string& s) { sink.push_back(s); }, nullptr,
        [](const std::string&, const std::vector<std::string>&, bool) -> std::optional<std::string> {
            return "5,6";
        });
    const auto r = interp.run("(setq p1 (getpoint)) (setq p2 (getpoint)) (command \"LINE\" p1 p2 \"\")");
    REQUIRE(r.ok);
    REQUIRE(sink == std::vector<std::string>{"LINE", "5,6", "5,6", ""});
}

TEST_CASE("LispInterpreter reports errors instead of crashing", "[lisp]") {
    REQUIRE_FALSE(run("(+ 1 \"a\")").ok);
    REQUIRE_FALSE(run("(undefined-fn 1)").ok);
    REQUIRE_FALSE(run("(/ 1 0)").ok);
    REQUIRE_FALSE(run("(setq)").ok);
    REQUIRE_FALSE(run("(car 1 2)").ok);
    REQUIRE_FALSE(run("(defun f (a) (+ a 1)) (f 1 2)").ok);
    REQUIRE_FALSE(run("unbound-symbol").ok); // bare symbol reference, unbound variable
    REQUIRE_FALSE(run("(+ 1 2").ok); // unmatched paren
}
