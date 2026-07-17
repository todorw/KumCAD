#include "core/document/SheetSet.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>

using namespace lcad;

TEST_CASE("addSheet creates its subset on demand and groups sheets by subset name", "[document][sheetset]") {
    SheetSet set;
    REQUIRE(addSheet(set, "Architectural", {"A-101", "First Floor Plan", "arch/floor1.dxf", "Layout1"}));
    REQUIRE(addSheet(set, "Architectural", {"A-102", "Second Floor Plan", "arch/floor2.dxf", "Layout1"}));
    REQUIRE(addSheet(set, "Structural", {"S-101", "Foundation Plan", "struct/foundation.dxf", "Layout1"}));

    REQUIRE(set.subsets.size() == 2);
    REQUIRE(set.sheetCount() == 3);
    REQUIRE(set.subsets[0].name == "Architectural");
    REQUIRE(set.subsets[0].sheets.size() == 2);
    REQUIRE(set.subsets[1].name == "Structural");
    REQUIRE(set.subsets[1].sheets.size() == 1);
}

TEST_CASE("addSheet rejects an empty subset name", "[document][sheetset]") {
    SheetSet set;
    REQUIRE_FALSE(addSheet(set, "", {"A-101", "Title", "path.dxf", "Layout1"}));
    REQUIRE(set.subsets.empty());
}

TEST_CASE("findSheet locates a sheet by number across every subset", "[document][sheetset]") {
    SheetSet set;
    addSheet(set, "Architectural", {"A-101", "First Floor Plan", "arch/floor1.dxf", "Layout1"});
    addSheet(set, "Structural", {"S-101", "Foundation Plan", "struct/foundation.dxf", "Layout1"});

    const SheetSetEntry* found = set.findSheet("S-101");
    REQUIRE(found != nullptr);
    REQUIRE(found->sheetTitle == "Foundation Plan");
    REQUIRE(set.findSheet("Z-999") == nullptr);
}

TEST_CASE("removeSheet deletes the matching sheet from whichever subset holds it", "[document][sheetset]") {
    SheetSet set;
    addSheet(set, "Architectural", {"A-101", "First Floor Plan", "arch/floor1.dxf", "Layout1"});
    addSheet(set, "Architectural", {"A-102", "Second Floor Plan", "arch/floor2.dxf", "Layout1"});

    REQUIRE(removeSheet(set, "A-101"));
    REQUIRE(set.sheetCount() == 1);
    REQUIRE(set.findSheet("A-101") == nullptr);
    REQUIRE(set.findSheet("A-102") != nullptr);
    REQUIRE_FALSE(removeSheet(set, "A-101")); // already gone
}

TEST_CASE("saveSheetSet/loadSheetSet round-trips a set with multiple subsets and sheets", "[document][sheetset]") {
    SheetSet set;
    set.name = "Project 123 Construction Set";
    set.description = "Contains spaces, and a comma too";
    addSheet(set, "Architectural", {"A-101", "First Floor Plan", "arch/floor1.dxf", "Layout1"});
    addSheet(set, "Architectural", {"A-102", "Second Floor Plan", "arch/floor2.dxf", "Layout1"});
    addSheet(set, "Structural", {"S-101", "Foundation Plan", "struct/foundation.dxf", "Layout1"});

    const std::string path = "/tmp/kumcad_sheetset_test.kss";
    std::string error;
    REQUIRE(saveSheetSet(set, path, &error));

    SheetSet loaded;
    REQUIRE(loadSheetSet(loaded, path, &error));
    REQUIRE(loaded.name == set.name);
    REQUIRE(loaded.description == set.description);
    REQUIRE(loaded.subsets.size() == 2);
    REQUIRE(loaded.subsets[0].name == "Architectural");
    REQUIRE(loaded.subsets[0].sheets.size() == 2);
    REQUIRE(loaded.subsets[0].sheets[0].sheetNumber == "A-101");
    REQUIRE(loaded.subsets[0].sheets[0].sheetTitle == "First Floor Plan");
    REQUIRE(loaded.subsets[0].sheets[0].drawingPath == "arch/floor1.dxf");
    REQUIRE(loaded.subsets[1].name == "Structural");
    REQUIRE(loaded.subsets[1].sheets[0].sheetNumber == "S-101");
    std::remove(path.c_str());
}

TEST_CASE("loadSheetSet rejects a file that isn't a .kss file", "[document][sheetset]") {
    const std::string path = "/tmp/kumcad_sheetset_bad_test.kss";
    {
        std::ofstream out(path);
        out << "NOTAKSS 1\n";
    }
    SheetSet loaded;
    std::string error;
    REQUIRE_FALSE(loadSheetSet(loaded, path, &error));
    REQUIRE_FALSE(error.empty());
    std::remove(path.c_str());
}

TEST_CASE("loadSheetSet reports an error for a missing file", "[document][sheetset]") {
    SheetSet loaded;
    std::string error;
    REQUIRE_FALSE(loadSheetSet(loaded, "/tmp/kumcad_sheetset_does_not_exist.kss", &error));
    REQUIRE_FALSE(error.empty());
}
