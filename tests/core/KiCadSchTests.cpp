#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/geometry/Wire.h"
#include "core/io/KiCadSch.h"
#include "core/io/SExpr.h"
#include "core/schematic/Netlist.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lcad;
using Catch::Approx;

namespace {

struct TempSchPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_kicadsch_test_" + std::to_string(std::rand()) + ".kicad_sch");
    ~TempSchPath() { std::filesystem::remove(path); }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// A minimal two-pin symbol (like a resistor): pin "1" at (0,0), pin "2" at
// (10,0) -- same fixture SchematicTests.cpp already uses.
const BlockDefinition* addTwoPinSymbol(Document& doc, const std::string& name) {
    doc.addBlock(name, {});
    BlockDefinition* block = doc.findBlock(name);
    block->pins.push_back(Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(-5, 0)});
    block->pins.push_back(Pin{"2", "2", PinElectricalType::Passive, Point2D(10, 0), Point2D(15, 0)});
    return block;
}

} // namespace

TEST_CASE("writeKiCadSch emits real wire/junction/no_connect/label/symbol S-expressions", "[dxf][kicad][sch]") {
    TempSchPath temp;
    Document doc;
    const BlockDefinition* r1 = addTwoPinSymbol(doc, "R");

    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r1, Point2D(0, 0));
    insert->setAttribute("REFDES", "R1");
    insert->setAttribute("VALUE", "10k");
    doc.addEntity(std::move(insert));

    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(10, 0), Point2D(50, 0), Point2D(50, 20)}));
    doc.addEntity(std::make_unique<JunctionEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(50, 0)));
    doc.addEntity(std::make_unique<NoConnectEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0)));
    doc.addEntity(
        std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(50, 20), "RESET"));

    std::string err;
    REQUIRE(writeKiCadSch(doc, temp.path.string(), &err));

    const std::string text = readFile(temp.path);
    auto root = parseSExpr(text);
    REQUIRE(root.has_value());
    REQUIRE(root->tag() == "kicad_sch");

    // A 3-vertex wire splits into 2 real 2-point KiCad wire segments.
    const auto wires = root->children("wire");
    REQUIRE(wires.size() == 2);

    REQUIRE(root->children("junction").size() == 1);
    REQUIRE(root->children("no_connect").size() == 1);

    const auto labels = root->children("label");
    REQUIRE(labels.size() == 1);
    REQUIRE(labels[0]->textAt(0) == "RESET");

    const auto symbols = root->children("symbol");
    REQUIRE(symbols.size() == 1);
    REQUIRE(symbols[0]->child("lib_id")->textAt(0) == "R");
    const auto props = symbols[0]->children("property");
    const auto refProp =
        std::find_if(props.begin(), props.end(), [](const SExpr* p) { return p->textAt(0) == "Reference"; });
    REQUIRE(refProp != props.end());
    REQUIRE((*refProp)->textAt(1) == "R1");
    const auto valueProp =
        std::find_if(props.begin(), props.end(), [](const SExpr* p) { return p->textAt(0) == "Value"; });
    REQUIRE(valueProp != props.end());
    REQUIRE((*valueProp)->textAt(1) == "10k");
}

TEST_CASE("writeKiCadSch + readKiCadSch round-trips real net connectivity through a fresh Document",
         "[dxf][kicad][sch]") {
    // The real end-to-end promise: a schematic with two symbols wired
    // together, written and reloaded from scratch, must still compute the
    // SAME net connectivity -- not just "the file parses."
    TempSchPath temp;
    Document doc;
    const BlockDefinition* r1 = addTwoPinSymbol(doc, "R");
    const BlockDefinition* r2 = addTwoPinSymbol(doc, "R2");

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r1, Point2D(0, 0));
    insertA->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r2, Point2D(100, 0));
    insertB->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(insertB));
    // R1 pin "2" (world (10,0)) to R2 pin "1" (world (100,0)).
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(10, 0), Point2D(100, 0)}));

    const std::vector<Net> originalNets = computeNets(doc);
    const auto originalWired =
        std::find_if(originalNets.begin(), originalNets.end(), [](const Net& n) { return n.pins.size() == 2; });
    REQUIRE(originalWired != originalNets.end());

    std::string err;
    REQUIRE(writeKiCadSch(doc, temp.path.string(), &err));

    Document loaded;
    REQUIRE(readKiCadSch(loaded, temp.path.string(), &err));

    const std::vector<Net> loadedNets = computeNets(loaded);
    REQUIRE(loadedNets.size() == originalNets.size());
    const auto loadedWired =
        std::find_if(loadedNets.begin(), loadedNets.end(), [](const Net& n) { return n.pins.size() == 2; });
    REQUIRE(loadedWired != loadedNets.end());

    // Resolve the reloaded net's two pins back to their own REFDES/pin
    // number (insert ids are fresh after reload, so compare by identity,
    // not raw id).
    auto refdesOf = [&](EntityId id) -> std::string {
        for (const Entity* e : loaded.entities()) {
            if (e->id() != id || e->type() != EntityType::Insert) continue;
            const auto* ins = static_cast<const InsertEntity*>(e);
            const std::string* rd = ins->attributeValue("REFDES");
            return rd ? *rd : std::string();
        }
        return {};
    };
    const bool hasR1Pin2 = std::any_of(loadedWired->pins.begin(), loadedWired->pins.end(), [&](const NetPin& p) {
        return refdesOf(p.insertId) == "R1" && p.pinNumber == "2";
    });
    const bool hasR2Pin1 = std::any_of(loadedWired->pins.begin(), loadedWired->pins.end(), [&](const NetPin& p) {
        return refdesOf(p.insertId) == "R2" && p.pinNumber == "1";
    });
    REQUIRE(hasR1Pin2);
    REQUIRE(hasR2Pin1);
}

TEST_CASE("readKiCadSch recovers real pin geometry/electrical types from a file's own lib_symbols table",
         "[dxf][kicad][sch]") {
    // A hand-built file mimicking real KiCad's own lib_symbols shape: a
    // multi-unit-style nested symbol with pins split across TWO nested
    // unit sub-symbols, proving collectPins' own recursive merge.
    TempSchPath temp;
    const std::string schText =
        "(kicad_sch\n"
        "  (version 20231120)\n"
        "  (generator \"kumcad_test\")\n"
        "  (lib_symbols\n"
        "    (symbol \"Device:R\"\n"
        "      (symbol \"R_0_1\"\n"
        "        (pin passive line (at 0 3.81 270) (length 1.27) (name \"~\") (number \"1\"))\n"
        "      )\n"
        "      (symbol \"R_1_1\"\n"
        "        (pin power_in line (at 0 -3.81 90) (length 1.27) (name \"~\") (number \"2\"))\n"
        "      )\n"
        "    )\n"
        "  )\n"
        "  (symbol (lib_id \"Device:R\") (at 50 60 0) (unit 1) (uuid \"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\")\n"
        "    (property \"Reference\" \"R1\" (at 52 58 0))\n"
        "    (property \"Value\" \"10k\" (at 52 60 0))\n"
        "  )\n"
        ")\n";
    std::ofstream out(temp.path, std::ios::binary);
    out << schText;
    out.close();

    Document doc;
    std::string err;
    REQUIRE(readKiCadSch(doc, temp.path.string(), &err));

    const BlockDefinition* block = doc.findBlock("Device:R");
    REQUIRE(block != nullptr);
    REQUIRE(block->pins.size() == 2); // merged from both unit sub-symbols

    const auto pin1 = std::find_if(block->pins.begin(), block->pins.end(), [](const Pin& p) { return p.number == "1"; });
    REQUIRE(pin1 != block->pins.end());
    REQUIRE(pin1->electricalType == PinElectricalType::Passive);
    REQUIRE(pin1->position.x == Approx(0.0));
    REQUIRE(pin1->position.y == Approx(3.81));

    const auto pin2 = std::find_if(block->pins.begin(), block->pins.end(), [](const Pin& p) { return p.number == "2"; });
    REQUIRE(pin2 != block->pins.end());
    REQUIRE(pin2->electricalType == PinElectricalType::Power);

    // One placed instance, its Reference/Value properties recovered too.
    bool sawInstance = false;
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* ins = static_cast<const InsertEntity*>(e);
        REQUIRE(ins->block() == block);
        const std::string* refdes = ins->attributeValue("REFDES");
        REQUIRE(refdes != nullptr);
        REQUIRE(*refdes == "R1");
        sawInstance = true;
    }
    REQUIRE(sawInstance);
}

TEST_CASE("readKiCadSch falls back to a shared pin-less placeholder block when lib_symbols has no matching entry",
         "[dxf][kicad][sch]") {
    // A file with no lib_symbols table at all (unlike writeKiCadSch's own
    // output, which always embeds one for every placed block -- see the
    // round-trip test above) -- two instances of the same lib_id must
    // still share ONE block, just with no pins.
    TempSchPath temp;
    const std::string schText =
        "(kicad_sch\n"
        "  (version 20231120)\n"
        "  (generator \"external_tool\")\n"
        "  (symbol (lib_id \"Unknown:Part\") (at 0 0 0) (unit 1)\n"
        "    (property \"Reference\" \"U1\" (at 0 0 0))\n"
        "  )\n"
        "  (symbol (lib_id \"Unknown:Part\") (at 100 0 0) (unit 1)\n"
        "    (property \"Reference\" \"U2\" (at 100 0 0))\n"
        "  )\n"
        ")\n";
    std::ofstream out(temp.path, std::ios::binary);
    out << schText;
    out.close();

    Document loaded;
    std::string err;
    REQUIRE(readKiCadSch(loaded, temp.path.string(), &err));

    const BlockDefinition* block = loaded.findBlock("Unknown:Part");
    REQUIRE(block != nullptr);
    REQUIRE(block->pins.empty());

    int instanceCount = 0;
    for (const Entity* e : loaded.entities()) {
        if (e->type() != EntityType::Insert) continue;
        REQUIRE(static_cast<const InsertEntity*>(e)->block() == block); // shared, not duplicated
        ++instanceCount;
    }
    REQUIRE(instanceCount == 2);
}

TEST_CASE("readKiCadSch fails cleanly on a missing or malformed file", "[dxf][kicad][sch]") {
    Document doc;
    std::string err;
    REQUIRE_FALSE(readKiCadSch(doc, "/nonexistent/path.kicad_sch", &err));
    REQUIRE_FALSE(err.empty());

    TempSchPath temp;
    std::ofstream out(temp.path, std::ios::binary);
    out << "(not_a_schematic)";
    out.close();
    REQUIRE_FALSE(readKiCadSch(doc, temp.path.string(), &err));
}
