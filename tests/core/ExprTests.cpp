#include "core/util/Expr.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;

TEST_CASE("evaluateExpression handles arithmetic and precedence", "[expr]") {
    REQUIRE(lcad::evaluateExpression("2+3*4").value() == Approx(14.0));
    REQUIRE(lcad::evaluateExpression("(2+3)*4").value() == Approx(20.0));
    REQUIRE(lcad::evaluateExpression("2^3^2").value() == Approx(512.0)); // right-associative
    REQUIRE(lcad::evaluateExpression("-2^2").value() == Approx(-4.0));   // unary binds looser than ^
    REQUIRE(lcad::evaluateExpression("10/4").value() == Approx(2.5));
    REQUIRE(lcad::evaluateExpression("  1 + 2  ").value() == Approx(3.0));
}

TEST_CASE("evaluateExpression supports constants and functions", "[expr]") {
    REQUIRE(lcad::evaluateExpression("pi").value() == Approx(M_PI));
    REQUIRE(lcad::evaluateExpression("sqrt(16)").value() == Approx(4.0));
    REQUIRE(lcad::evaluateExpression("sin(90)").value() == Approx(1.0));
    REQUIRE(lcad::evaluateExpression("cos(0)").value() == Approx(1.0));
    REQUIRE(lcad::evaluateExpression("atan2(1,1)").value() == Approx(45.0));
    REQUIRE(lcad::evaluateExpression("max(3,7)").value() == Approx(7.0));
    REQUIRE(lcad::evaluateExpression("min(3,7)").value() == Approx(3.0));
    REQUIRE(lcad::evaluateExpression("abs(-5)").value() == Approx(5.0));
}

TEST_CASE("evaluateExpression reports errors instead of crashing", "[expr]") {
    std::string error;
    REQUIRE_FALSE(lcad::evaluateExpression("1/0", &error).has_value());
    REQUIRE_FALSE(error.empty());

    REQUIRE_FALSE(lcad::evaluateExpression("(1+2", &error).has_value());
    REQUIRE_FALSE(lcad::evaluateExpression("1 + ", &error).has_value());
    REQUIRE_FALSE(lcad::evaluateExpression("bogus(1)", &error).has_value());
    REQUIRE_FALSE(lcad::evaluateExpression("sqrt(-1)", &error).has_value());
    REQUIRE_FALSE(lcad::evaluateExpression("sqrt(1,2)", &error).has_value());
    REQUIRE_FALSE(lcad::evaluateExpression("1 2", &error).has_value());
}

TEST_CASE("evaluateExpression with a variable lookup resolves bare identifiers", "[expr][variables]") {
    auto lookup = [](const std::string& name) -> std::optional<double> {
        if (name == "Width") return 10.0;
        if (name == "Height") return 5.0;
        return std::nullopt;
    };

    REQUIRE(lcad::evaluateExpression("Width", lookup).value() == Approx(10.0));
    REQUIRE(lcad::evaluateExpression("Width*2+Height", lookup).value() == Approx(25.0));
    REQUIRE(lcad::evaluateExpression("sqrt(Width*Width+Height*Height)", lookup).value() == Approx(std::sqrt(125.0)));

    // Built-ins still work exactly as before, unaffected by the lookup.
    REQUIRE(lcad::evaluateExpression("pi*2", lookup).value() == Approx(2.0 * M_PI));
    REQUIRE(lcad::evaluateExpression("sin(90)", lookup).value() == Approx(1.0));
}

TEST_CASE("evaluateExpression variable lookup is case-sensitive on the ORIGINAL name", "[expr][variables]") {
    auto lookup = [](const std::string& name) -> std::optional<double> {
        if (name == "Width") return 10.0;
        if (name == "width") return 99.0; // a deliberately different variable
        return std::nullopt;
    };
    REQUIRE(lcad::evaluateExpression("Width", lookup).value() == Approx(10.0));
    REQUIRE(lcad::evaluateExpression("width", lookup).value() == Approx(99.0));
}

TEST_CASE("evaluateExpression fails cleanly when a variable is unknown", "[expr][variables]") {
    auto lookup = [](const std::string&) -> std::optional<double> { return std::nullopt; };
    std::string error;
    REQUIRE_FALSE(lcad::evaluateExpression("NoSuchVar", lookup, &error).has_value());
    REQUIRE_FALSE(error.empty());

    // No lookup at all (default single-arg overload) still fails on any
    // bare identifier that isn't pi/e, exactly as before.
    REQUIRE_FALSE(lcad::evaluateExpression("Width").has_value());
}
