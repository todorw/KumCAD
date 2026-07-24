#include "core/document/Document.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/Wire.h"
#include "core/io/KiCadSch.h"
#include "core/io/SExpr.h"
#include "core/schematic/Sheets.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lcad;

namespace {

struct TempDir {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_kicadsch_hier_" + std::to_string(std::rand()));
    TempDir() { std::filesystem::create_directory(path); }
    ~TempDir() { std::filesystem::remove_all(path); }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

} // namespace

TEST_CASE("writeKiCadSchHierarchical falls back to a flat single file when the document has no sheets",
         "[dxf][kicad][sch][hierarchical]") {
    TempDir dir;
    Document doc;
    doc.addEntity(
        std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(), std::vector<Point2D>{{0, 0}, {10, 0}}));

    const std::string rootPath = (dir.path / "root.kicad_sch").string();
    std::string err;
    REQUIRE(writeKiCadSchHierarchical(doc, rootPath, &err));

    auto root = parseSExpr(readFile(rootPath));
    REQUIRE(root.has_value());
    REQUIRE(root->children("wire").size() == 1);
    REQUIRE(root->children("sheet").empty());
}

TEST_CASE("writeKiCadSchHierarchical splits sheet content into real per-sheet files with (sheet ...) "
         "cross-references in the root",
         "[dxf][kicad][sch][hierarchical]") {
    TempDir dir;
    Document doc;
    doc.addLayer("0", Color{255, 255, 255});
    const LayerId powerLayer = createSheet(doc, "Power");
    const LayerId controlLayer = createSheet(doc, "Control");

    // Common content, on the plain "0" layer (not a sheet layer at all).
    doc.addEntity(std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0),
                                                    "COMMON_NET"));
    // Power-sheet-only content.
    doc.addEntity(
        std::make_unique<WireEntity>(doc.reserveEntityId(), powerLayer, std::vector<Point2D>{{0, 0}, {10, 0}}));
    // Control-sheet-only content.
    doc.addEntity(
        std::make_unique<WireEntity>(doc.reserveEntityId(), controlLayer, std::vector<Point2D>{{0, 0}, {5, 5}}));

    const std::string rootPath = (dir.path / "root.kicad_sch").string();
    std::string err;
    REQUIRE(writeKiCadSchHierarchical(doc, rootPath, &err));

    auto root = parseSExpr(readFile(rootPath));
    REQUIRE(root.has_value());
    REQUIRE(root->tag() == "kicad_sch");

    // The root file itself carries only the common content...
    REQUIRE(root->children("label").size() == 1);
    REQUIRE(root->children("wire").empty()); // both wires live in their own sheet files, not here

    // ...and two real (sheet ...) cross-references, one per sheet.
    const auto sheetExprs = root->children("sheet");
    REQUIRE(sheetExprs.size() == 2);
    for (const SExpr* sheetExpr : sheetExprs) {
        const auto props = sheetExpr->children("property");
        bool sawSheetfile = false;
        std::string sheetFileName;
        for (const SExpr* prop : props) {
            if (prop->textAt(0) == "Sheetfile") {
                sawSheetfile = true;
                sheetFileName = prop->textAt(1);
            }
        }
        REQUIRE(sawSheetfile);

        // The referenced file must actually exist on disk, alongside the
        // root, and contain that sheet's own wire.
        const std::filesystem::path childPath = dir.path / sheetFileName;
        REQUIRE(std::filesystem::exists(childPath));
        auto child = parseSExpr(readFile(childPath));
        REQUIRE(child.has_value());
        REQUIRE(child->tag() == "kicad_sch");
        REQUIRE(child->children("wire").size() == 1);
    }
}
