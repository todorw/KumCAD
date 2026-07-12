#include "core/io/Zip.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

struct TempZipPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_zip_test_" + std::to_string(std::rand()) + ".zip");
    ~TempZipPath() { std::filesystem::remove(path); }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

} // namespace

TEST_CASE("writeZip produces a well-formed archive with embedded content", "[zip]") {
    TempZipPath temp;
    const std::vector<std::pair<std::string, std::string>> entries{
        {"drawing.dxf", "0\nSECTION\n...\n"},
        {"refs/logo.png", std::string(500, 'x')}, // binary-ish payload, larger than one "line"
    };
    REQUIRE(lcad::writeZip(temp.path.string(), entries));

    const std::string data = readFile(temp.path);
    REQUIRE_FALSE(data.empty());

    // Local file header signature, once per entry.
    const std::string localSig{"\x50\x4b\x03\x04", 4};
    std::size_t count = 0;
    for (std::size_t pos = data.find(localSig); pos != std::string::npos; pos = data.find(localSig, pos + 1)) ++count;
    REQUIRE(count == entries.size());

    // Central directory and end-of-central-directory records are present.
    REQUIRE(data.find(std::string{"\x50\x4b\x01\x02", 4}) != std::string::npos);
    REQUIRE(data.find(std::string{"\x50\x4b\x05\x06", 4}) != std::string::npos);

    // Store method: names and raw content appear verbatim (no compression).
    for (const auto& [name, content] : entries) {
        REQUIRE(data.find(name) != std::string::npos);
        REQUIRE(data.find(content) != std::string::npos);
    }
}

TEST_CASE("writeZip handles an empty entry list", "[zip]") {
    TempZipPath temp;
    REQUIRE(lcad::writeZip(temp.path.string(), {}));
    REQUIRE(std::filesystem::file_size(temp.path) == 22); // just the end-of-central-directory record
}
