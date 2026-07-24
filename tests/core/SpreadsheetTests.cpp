#include "core/document/Spreadsheet.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace lcad;
using Catch::Approx;

TEST_CASE("Spreadsheet stores and resolves a plain numeric literal cell", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "42");
    REQUIRE(sheet.hasCell("A1"));
    const auto v = sheet.value("A1");
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(42.0));
}

TEST_CASE("Spreadsheet evaluates a formula against a plain arithmetic expression", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "=2+3*4");
    const auto v = sheet.value("A1");
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(14.0));
}

TEST_CASE("Spreadsheet resolves a formula referencing another cell", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "10");
    sheet.setCell("B1", "=A1*2");
    const auto v = sheet.value("B1");
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(20.0));
}

TEST_CASE("Spreadsheet resolves a formula chain transitively through several cells", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "5");
    sheet.setCell("B1", "=A1+1");
    sheet.setCell("C1", "=B1*B1");
    const auto v = sheet.value("C1");
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(36.0)); // (5+1)^2
}

TEST_CASE("Spreadsheet detects a direct circular reference instead of infinite-looping", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "=B1");
    sheet.setCell("B1", "=A1");
    std::string error;
    const auto v = sheet.value("A1", &error);
    REQUIRE_FALSE(v.has_value());
    REQUIRE(error == "circular cell reference");
}

TEST_CASE("Spreadsheet detects a longer, transitive circular reference", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "=B1");
    sheet.setCell("B1", "=C1");
    sheet.setCell("C1", "=A1");
    REQUIRE_FALSE(sheet.value("A1").has_value());
}

TEST_CASE("Spreadsheet reports an error for an unset or non-numeric cell", "[spreadsheet]") {
    Spreadsheet sheet;
    std::string error;
    REQUIRE_FALSE(sheet.value("Z9", &error).has_value());
    REQUIRE(error == "empty cell");

    sheet.setCell("A1", "hello");
    REQUIRE_FALSE(sheet.value("A1").has_value());

    sheet.setCell("B1", "=A1+1"); // formula referencing a non-numeric cell
    REQUIRE_FALSE(sheet.value("B1").has_value());
}

TEST_CASE("Spreadsheet clearCell removes a cell and setCell with empty content has the same effect",
         "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "42");
    REQUIRE(sheet.hasCell("A1"));
    sheet.clearCell("A1");
    REQUIRE_FALSE(sheet.hasCell("A1"));

    sheet.setCell("B1", "1");
    sheet.setCell("B1", "");
    REQUIRE_FALSE(sheet.hasCell("B1"));
}

TEST_CASE("Spreadsheet cellNames lists exactly the cells currently set", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "1");
    sheet.setCell("B2", "2");
    const auto names = sheet.cellNames();
    REQUIRE(names.size() == 2);
    REQUIRE(std::find(names.begin(), names.end(), "A1") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "B2") != names.end());
}

TEST_CASE("Spreadsheet rawContent returns the exact original text, formula prefix included", "[spreadsheet]") {
    Spreadsheet sheet;
    sheet.setCell("A1", "=A2+1");
    REQUIRE(sheet.rawContent("A1") == "=A2+1");
    REQUIRE(sheet.rawContent("NoSuchCell").empty());
}
