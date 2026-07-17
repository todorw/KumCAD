#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/pcb/Ratsnest.h"
#include "core/pcb/SpecctraWriter.h"
#include "core/schematic/SymbolLibrary.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lcad;

namespace {
struct TempPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_dsn_test_" + std::to_string(std::rand()) + ".dsn");
    ~TempPath() { std::filesystem::remove(path); }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}
} // namespace

TEST_CASE("writeSpecctraDsn emits a well-formed DSN with placement, library, and network sections", "[pcb][dsn]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    REQUIRE(rfp);

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    insertA->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(50, 0));
    insertB->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(insertB));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "2"}, {"R2", "1"}};

    TempPath temp;
    std::string error;
    REQUIRE(writeSpecctraDsn(doc, {net}, temp.path.string(), &error));

    const std::string content = readFile(temp.path);
    REQUIRE(content.find("(pcb \"kumcad_board\"") != std::string::npos);
    REQUIRE(content.find("(structure") != std::string::npos);
    REQUIRE(content.find("(boundary") != std::string::npos);
    REQUIRE(content.find("(placement") != std::string::npos);
    REQUIRE(content.find("R1") != std::string::npos);
    REQUIRE(content.find("R2") != std::string::npos);
    REQUIRE(content.find("(library") != std::string::npos);
    REQUIRE(content.find("(padstack") != std::string::npos);
    REQUIRE(content.find("(image R_FP") != std::string::npos);
    REQUIRE(content.find("(network") != std::string::npos);
    REQUIRE(content.find("\"Net1\"") != std::string::npos);
    REQUIRE(content.find("R1-2") != std::string::npos);
    REQUIRE(content.find("R2-1") != std::string::npos);

    // Balanced parentheses -- a basic well-formedness check for an
    // s-expression format like DSN.
    int depth = 0;
    for (char c : content) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        REQUIRE(depth >= 0);
    }
    REQUIRE(depth == 0);
}

TEST_CASE("writeSpecctraDsn reuses one image per distinct footprint across multiple placements", "[pcb][dsn]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    REQUIRE(rfp);

    for (int i = 0; i < 3; ++i) {
        auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp,
                                                      Point2D(50.0 * i, 0));
        insert->setAttribute("REFDES", "R" + std::to_string(i + 1));
        doc.addEntity(std::move(insert));
    }

    TempPath temp;
    std::string error;
    REQUIRE(writeSpecctraDsn(doc, {}, temp.path.string(), &error));

    const std::string content = readFile(temp.path);
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = content.find("(image R_FP", pos)) != std::string::npos) {
        ++count;
        pos += 1;
    }
    REQUIRE(count == 1); // one image definition, reused by all 3 placements
}

TEST_CASE("writeSpecctraDsn fails cleanly with no placed footprints", "[pcb][dsn]") {
    Document doc;
    std::string error;
    REQUIRE_FALSE(writeSpecctraDsn(doc, {}, "/tmp/kumcad_dsn_unused.dsn", &error));
    REQUIRE_FALSE(error.empty());
}
