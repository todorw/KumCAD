#include "core/io/Csv.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {

struct TempCsvPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_csv_test_" + std::to_string(std::rand()) + ".csv");
    ~TempCsvPath() { std::filesystem::remove(path); }
};

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

TEST_CASE("readCsv parses plain rows and pads to a rectangular grid", "[csv]") {
    TempCsvPath temp;
    writeFile(temp.path, "a,b,c\n1,2\nx,y,z,w\n");

    const auto rows = lcad::readCsv(temp.path.string());
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 3);
    REQUIRE((*rows)[0] == std::vector<std::string>{"a", "b", "c", ""});
    REQUIRE((*rows)[1] == std::vector<std::string>{"1", "2", "", ""});
    REQUIRE((*rows)[2] == std::vector<std::string>{"x", "y", "z", "w"});
}

TEST_CASE("readCsv handles quoted fields with commas and escaped quotes", "[csv]") {
    TempCsvPath temp;
    writeFile(temp.path, "\"Smith, John\",\"He said \"\"hi\"\"\",42\n");

    const auto rows = lcad::readCsv(temp.path.string());
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);
    REQUIRE((*rows)[0][0] == "Smith, John");
    REQUIRE((*rows)[0][1] == "He said \"hi\"");
    REQUIRE((*rows)[0][2] == "42");
}

TEST_CASE("readCsv reports failure for a missing file", "[csv]") {
    std::string error;
    REQUIRE_FALSE(lcad::readCsv("/no/such/file.csv", &error).has_value());
    REQUIRE_FALSE(error.empty());
}
