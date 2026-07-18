#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/pcb/Drc.h"
#include "core/pcb/GerberWriter.h"
#include "core/pcb/Ratsnest.h"
#include "core/pcb/ViaStitching.h"
#include "core/schematic/SymbolLibrary.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lcad;

namespace {
struct TempPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_pcb_test_" + std::to_string(std::rand()) + ".txt");
    ~TempPath() { std::filesystem::remove(path); }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}
} // namespace

TEST_CASE("parseNetlist reads formatNetlist's own output back", "[pcb][netlist]") {
    const std::string text = "# KumCAD netlist\nNET \"VCC\"\n  R1.1\n  U1.8\nNET \"Net1\"\n  R1.2\n  R2.1\n";
    const std::vector<ImportedNet> nets = parseNetlist(text);

    REQUIRE(nets.size() == 2);
    REQUIRE(nets[0].name == "VCC");
    REQUIRE(nets[0].pins.size() == 2);
    REQUIRE(nets[0].pins[0].refDes == "R1");
    REQUIRE(nets[0].pins[0].pinNumber == "1");
    REQUIRE(nets[1].name == "Net1");
    REQUIRE(nets[1].pins.size() == 2);
}

TEST_CASE("computeRatsnest connects two placed footprints on the same net with no copper yet",
         "[pcb][ratsnest]") {
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

    const std::vector<RatsnestLine> lines = computeRatsnest(doc, {net});
    REQUIRE(lines.size() == 1);
    // R1 pad 2 is at world (10,0), R2 pad 1 is at world (50,0).
    const bool matches = (lines[0].a.distanceTo(Point2D(10, 0)) < 1e-6 && lines[0].b.distanceTo(Point2D(50, 0)) < 1e-6) ||
                        (lines[0].b.distanceTo(Point2D(10, 0)) < 1e-6 && lines[0].a.distanceTo(Point2D(50, 0)) < 1e-6);
    REQUIRE(matches);
}

TEST_CASE("computeRatsnest finds no airwire once a Track already joins the pads", "[pcb][ratsnest]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    insertA->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(50, 0));
    insertB->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(insertB));
    // R1 pad 2 (10,0) to R2 pad 1 (50,0), already routed.
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(10, 0), Point2D(50, 0)}));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "2"}, {"R2", "1"}};

    const std::vector<RatsnestLine> lines = computeRatsnest(doc, {net});
    REQUIRE(lines.empty());
}

TEST_CASE("registerBuiltinSymbols adds usable footprints alongside symbols", "[pcb][library]") {
    Document doc;
    registerBuiltinSymbols(doc);

    const BlockDefinition* rfp = doc.findBlock("R_FP");
    REQUIRE(rfp);
    REQUIRE(rfp->isFootprint());
    REQUIRE(rfp->pads.size() == 2);

    const BlockDefinition* icfp = doc.findBlock("IC_FP");
    REQUIRE(icfp);
    REQUIRE(icfp->pads.size() == 4);
}

TEST_CASE("runDrc flags a track under the minimum width", "[pcb][drc]") {
    Document doc;
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0)}, 0.05));
    const std::vector<DrcViolation> violations = runDrc(doc);
    const bool hasWidthIssue = std::any_of(violations.begin(), violations.end(),
                                           [](const DrcViolation& v) { return v.message.find("width") != std::string::npos; });
    REQUIRE(hasWidthIssue);
}

TEST_CASE("runDrc flags a via whose drill isn't smaller than its own pad", "[pcb][drc]") {
    Document doc;
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0), 0.5, 0.6));
    const std::vector<DrcViolation> violations = runDrc(doc);
    const bool hasDrillIssue = std::any_of(violations.begin(), violations.end(),
                                           [](const DrcViolation& v) { return v.message.find("drill") != std::string::npos; });
    REQUIRE(hasDrillIssue);
}

TEST_CASE("runDrc flags overlapping footprint courtyards only when checkCourtyards is enabled", "[pcb][drc]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    auto r1 = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    const EntityId r1Id = r1->id();
    doc.addEntity(std::move(r1));
    // R_FP's own body spans local x in [-2,12] -- placed only 2 units
    // away, R2's courtyard (even before any margin) already overlaps R1's.
    auto r2 = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(2, 0));
    doc.addEntity(std::move(r2));

    const std::vector<DrcViolation> defaultRun = runDrc(doc);
    REQUIRE(std::none_of(defaultRun.begin(), defaultRun.end(),
                        [](const DrcViolation& v) { return v.message.find("Courtyard") != std::string::npos; }));

    DrcRules rules;
    rules.checkCourtyards = true;
    const std::vector<DrcViolation> checkedRun = runDrc(doc, rules);
    const bool flagged = std::any_of(checkedRun.begin(), checkedRun.end(), [&](const DrcViolation& v) {
        return v.entityId == r1Id && v.message.find("Courtyard") != std::string::npos;
    });
    REQUIRE(flagged);
}

TEST_CASE("runDrc does not flag well-separated footprints' courtyards", "[pcb][drc]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0)));
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(100, 0)));

    DrcRules rules;
    rules.checkCourtyards = true;
    const std::vector<DrcViolation> violations = runDrc(doc, rules);
    REQUIRE(std::none_of(violations.begin(), violations.end(),
                        [](const DrcViolation& v) { return v.message.find("Courtyard") != std::string::npos; }));
}

TEST_CASE("runDrc flags silkscreen over a pad only when checkSilkscreenOverPad is enabled", "[pcb][drc]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    auto r1 = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    const EntityId r1Id = r1->id();
    doc.addEntity(std::move(r1));
    // R1's own silkscreen top edge sits at world y=2 (local body top).
    // R2's own pad 1 (local (0,0)) lands exactly there.
    auto r2 = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(5, 2));
    doc.addEntity(std::move(r2));

    const std::vector<DrcViolation> defaultRun = runDrc(doc);
    REQUIRE(std::none_of(defaultRun.begin(), defaultRun.end(), [&](const DrcViolation& v) {
        return v.entityId == r1Id && v.message.find("Silkscreen") != std::string::npos;
    }));

    DrcRules rules;
    rules.checkSilkscreenOverPad = true;
    const std::vector<DrcViolation> checkedRun = runDrc(doc, rules);
    const bool flagged = std::any_of(checkedRun.begin(), checkedRun.end(), [&](const DrcViolation& v) {
        return v.entityId == r1Id && v.message.find("Silkscreen") != std::string::npos;
    });
    REQUIRE(flagged);
}

TEST_CASE("runDrc flags unconnected copper closer than the clearance and clears once joined",
         "[pcb][drc]") {
    Document doc;
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0), 0.6, 0.3));
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0.5, 0), 0.6, 0.3));

    DrcRules rules;
    const std::vector<DrcViolation> violations = runDrc(doc, rules);
    const bool hasClearanceIssue = std::any_of(
        violations.begin(), violations.end(), [](const DrcViolation& v) { return v.message.find("Clearance") != std::string::npos; });
    REQUIRE(hasClearanceIssue);

    // Joining them with a track (same connectivity component) clears the
    // violation even though they're still physically close.
    Document doc2;
    doc2.addEntity(std::make_unique<ViaEntity>(doc2.reserveEntityId(), doc2.currentLayer(), Point2D(0, 0), 0.6, 0.3));
    doc2.addEntity(std::make_unique<ViaEntity>(doc2.reserveEntityId(), doc2.currentLayer(), Point2D(0.5, 0), 0.6, 0.3));
    doc2.addEntity(std::make_unique<TrackEntity>(doc2.reserveEntityId(), doc2.currentLayer(),
                                                 std::vector<Point2D>{Point2D(0, 0), Point2D(0.5, 0)}, 0.25));
    const std::vector<DrcViolation> violations2 = runDrc(doc2, rules);
    const bool stillHasClearanceIssue = std::any_of(
        violations2.begin(), violations2.end(), [](const DrcViolation& v) { return v.message.find("Clearance") != std::string::npos; });
    REQUIRE_FALSE(stillHasClearanceIssue);
}

TEST_CASE("writeGerberLayer emits a well-formed RS-274X file with apertures, draws, and flashes",
         "[pcb][gerber]") {
    TempPath temp;
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    insert->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insert));
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(10, 0), Point2D(20, 0)}, 0.25));
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(20, 0), 0.6, 0.3));

    REQUIRE(writeGerberLayer(doc, doc.currentLayer(), temp.path.string()));
    const std::string text = readFile(temp.path);

    REQUIRE(text.find("%FSLAX35Y35*%") != std::string::npos);
    REQUIRE(text.find("%MOMM*%") != std::string::npos);
    REQUIRE(text.find("%ADD10C,") != std::string::npos); // track width aperture
    REQUIRE(text.find("D02*") != std::string::npos);     // track move
    REQUIRE(text.find("D01*") != std::string::npos);     // track draw
    REQUIRE(text.find("D03*") != std::string::npos);     // via/pad flash
    REQUIRE(text.find("M02*") != std::string::npos);
}

TEST_CASE("writeGerberLayer emits real Gerber X2 file attributes with FileFunction inferred from the layer name",
         "[pcb][gerber]") {
    TempPath temp;
    Document doc;
    const LayerId cuLayer = doc.addLayer("F.Cu", Color(0, 0, 0));
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), cuLayer,
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0)}, 0.25));

    REQUIRE(writeGerberLayer(doc, cuLayer, temp.path.string()));
    const std::string text = readFile(temp.path);

    REQUIRE(text.find("%TF.GenerationSoftware,KumCAD,") != std::string::npos);
    REQUIRE(text.find("%TF.CreationDate,") != std::string::npos);
    REQUIRE(text.find("%TF.FileFunction,Copper,L1,Top*%") != std::string::npos);
    REQUIRE(text.find("%TF.FilePolarity,Positive*%") != std::string::npos);

    TempPath temp2;
    Document doc2;
    const LayerId silkLayer = doc2.addLayer("B.SilkS", Color(0, 0, 0));
    doc2.addEntity(std::make_unique<TrackEntity>(doc2.reserveEntityId(), silkLayer,
                                                 std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0)}, 0.25));
    REQUIRE(writeGerberLayer(doc2, silkLayer, temp2.path.string()));
    REQUIRE(readFile(temp2.path).find("%TF.FileFunction,Legend,Bot*%") != std::string::npos);

    TempPath temp3;
    Document doc3;
    const LayerId innerLayer = doc3.addLayer("In2.Cu", Color(0, 0, 0));
    doc3.addEntity(std::make_unique<TrackEntity>(doc3.reserveEntityId(), innerLayer,
                                                 std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0)}, 0.25));
    REQUIRE(writeGerberLayer(doc3, innerLayer, temp3.path.string()));
    REQUIRE(readFile(temp3.path).find("%TF.FileFunction,Copper,L2,Inr*%") != std::string::npos);
}

TEST_CASE("writeGerberLayer wraps each footprint's pad flashes in %TO.C%/%TD*% naming its REFDES",
         "[pcb][gerber]") {
    TempPath temp;
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    insert->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insert));

    REQUIRE(writeGerberLayer(doc, doc.currentLayer(), temp.path.string()));
    const std::string text = readFile(temp.path);

    const std::size_t openPos = text.find("%TO.C,R1*%");
    REQUIRE(openPos != std::string::npos);
    const std::size_t closePos = text.find("%TD*%", openPos);
    REQUIRE(closePos != std::string::npos);
    const std::size_t flashPos = text.find("D03*", openPos);
    REQUIRE(flashPos != std::string::npos);
    REQUIRE(flashPos < closePos);
}

TEST_CASE("writeGerberLayer draws a solid Hatch as a G36/G37 region (a copper pour)", "[pcb][gerber]") {
    TempPath temp;
    Document doc;
    doc.addEntity(std::make_unique<HatchEntity>(
        doc.reserveEntityId(), doc.currentLayer(),
        std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0), Point2D(10, 10), Point2D(0, 10)}, HatchPattern::Solid,
        1.0, 0.0));

    REQUIRE(writeGerberLayer(doc, doc.currentLayer(), temp.path.string()));
    const std::string text = readFile(temp.path);
    REQUIRE(text.find("G36*") != std::string::npos);
    REQUIRE(text.find("G37*") != std::string::npos);
}

TEST_CASE("writeExcellonDrill lists one tool per distinct drill diameter", "[pcb][drill]") {
    TempPath temp;
    Document doc;
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0), 0.6, 0.3));
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(10, 0), 0.6, 0.3));
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(20, 0), 0.8, 0.5));

    REQUIRE(writeExcellonDrill(doc, temp.path.string()));
    const std::string text = readFile(temp.path);

    REQUIRE(text.find("M48") != std::string::npos);
    REQUIRE(text.find("T01") != std::string::npos);
    REQUIRE(text.find("T02") != std::string::npos);
    REQUIRE(text.find("M30") != std::string::npos);
    // Two distinct diameters (0.3, 0.5) means exactly two tool definitions,
    // not three holes each with their own tool ("METRIC" itself contains a
    // C, so count "T0<n>C" tool-definition markers specifically).
    int toolDefCount = 0;
    for (std::size_t pos = text.find("T0"); pos != std::string::npos; pos = text.find("T0", pos + 1)) {
        if (pos + 3 < text.size() && text[pos + 3] == 'C') ++toolDefCount;
    }
    REQUIRE(toolDefCount == 2);
}

TEST_CASE("stitchVias places roughly perimeter/spacing vias inset from a square boundary's edge",
         "[pcb][via-stitching]") {
    Document doc;
    const std::vector<Point2D> boundary = {Point2D(0, 0), Point2D(20, 0), Point2D(20, 20), Point2D(0, 20)};

    const std::vector<EntityId> ids = stitchVias(doc, doc.currentLayer(), boundary, 5.0, 1.0);
    // 80mm perimeter / 5mm spacing = 16 vias, give or take rounding to a
    // whole step count.
    REQUIRE(ids.size() >= 14);
    REQUIRE(ids.size() <= 18);

    for (const EntityId id : ids) {
        const Entity* e = doc.findEntity(id);
        REQUIRE(e != nullptr);
        REQUIRE(e->type() == EntityType::Via);
        const auto* via = static_cast<const ViaEntity*>(e);
        // Every via must land strictly inside the original square, not on
        // or outside its edge -- confirms the inward inset actually moved
        // it off the boundary.
        REQUIRE(via->position().x > 0.0);
        REQUIRE(via->position().x < 20.0);
        REQUIRE(via->position().y > 0.0);
        REQUIRE(via->position().y < 20.0);
    }
}

TEST_CASE("stitchVias applies the requested diameter and drill diameter to every placed via",
         "[pcb][via-stitching]") {
    Document doc;
    const std::vector<Point2D> boundary = {Point2D(0, 0), Point2D(20, 0), Point2D(20, 20), Point2D(0, 20)};

    const std::vector<EntityId> ids = stitchVias(doc, doc.currentLayer(), boundary, 5.0, 1.0, 0.8, 0.4);
    REQUIRE_FALSE(ids.empty());
    for (const EntityId id : ids) {
        const auto* via = static_cast<const ViaEntity*>(doc.findEntity(id));
        REQUIRE(via->diameter() == Catch::Approx(0.8));
        REQUIRE(via->drillDiameter() == Catch::Approx(0.4));
    }
}

TEST_CASE("stitchVias returns no vias for a degenerate boundary or non-positive spacing",
         "[pcb][via-stitching]") {
    Document doc;
    REQUIRE(stitchVias(doc, doc.currentLayer(), {Point2D(0, 0), Point2D(1, 1)}, 5.0, 1.0).empty());

    const std::vector<Point2D> boundary = {Point2D(0, 0), Point2D(20, 0), Point2D(20, 20), Point2D(0, 20)};
    REQUIRE(stitchVias(doc, doc.currentLayer(), boundary, 0.0, 1.0).empty());
}

TEST_CASE("writePickAndPlace lists each footprint's reference designator and position", "[pcb][pnp]") {
    TempPath temp;
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(5, 7));
    insert->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insert));

    REQUIRE(writePickAndPlace(doc, temp.path.string()));
    const std::string text = readFile(temp.path);

    REQUIRE(text.find("RefDes,Footprint,X,Y,RotationDeg") != std::string::npos);
    REQUIRE(text.find("R1,R_FP,5,7") != std::string::npos);
}
