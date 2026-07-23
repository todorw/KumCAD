#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/io/KiCadPcb.h"
#include "core/io/SExpr.h"
#include "core/pcb/Ratsnest.h"
#include "core/schematic/SymbolLibrary.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lcad;
using Catch::Approx;

namespace {
struct TempPcbPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_kicadpcb_test_" + std::to_string(std::rand()) + ".kicad_pcb");
    ~TempPcbPath() { std::filesystem::remove(path); }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}
} // namespace

TEST_CASE("writeKiCadPcb assigns real touch-connectivity net numbers to a track and leaves an isolated via "
         "unconnected",
         "[dxf][kicad][pcb]") {
    TempPcbPath temp;
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    REQUIRE(rfp);
    REQUIRE(rfp->pads.size() >= 2);

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    insertA->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(50, 0));
    insertB->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(insertB));
    // R1 pad 2 (world (10,0)) to R2 pad 1 (world (50,0)) -- same as the
    // existing computeRatsnest fixture in PcbTests.cpp.
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(10, 0), Point2D(50, 0)}, 0.3));
    // An isolated via, touching neither the net's pads nor the track.
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(200, 200)));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "2"}, {"R2", "1"}};

    std::string err;
    REQUIRE(writeKiCadPcb(doc, {net}, temp.path.string(), &err));

    const std::string text = readFile(temp.path);
    auto root = parseSExpr(text);
    REQUIRE(root.has_value());
    REQUIRE(root->tag() == "kicad_pcb");

    // Net table: 0 = "", 1 = "Net1".
    const auto netExprs = root->children("net");
    REQUIRE(netExprs.size() == 2);
    REQUIRE(netExprs[0]->numberAt(0) == Approx(0));
    REQUIRE(netExprs[1]->numberAt(0) == Approx(1));
    REQUIRE(netExprs[1]->textAt(1) == "Net1");

    // The track's segment carries net 1 (real touch-connectivity to both pads).
    const auto segments = root->children("segment");
    REQUIRE(segments.size() == 1);
    const SExpr* segNet = segments[0]->child("net");
    REQUIRE(segNet != nullptr);
    REQUIRE(segNet->numberAt(0) == Approx(1));

    // The isolated via carries net 0 (no connection resolved).
    const auto viaExprs = root->children("via");
    REQUIRE(viaExprs.size() == 1);
    const SExpr* viaNet = viaExprs[0]->child("net");
    REQUIRE(viaNet != nullptr);
    REQUIRE(viaNet->numberAt(0) == Approx(0));

    // Both footprints wrote a (net 1 "Net1") tag on their connected pad.
    const auto footprintExprs = root->children("footprint");
    REQUIRE(footprintExprs.size() == 2);
    bool foundConnectedPad = false;
    for (const SExpr* fp : footprintExprs) {
        for (const SExpr* pad : fp->children("pad")) {
            if (const SExpr* padNet = pad->child("net")) {
                if (padNet->numberAt(0) == Approx(1)) foundConnectedPad = true;
            }
        }
    }
    REQUIRE(foundConnectedPad);
}

TEST_CASE("readKiCadPcb reconstructs footprint placements, track geometry, and via geometry",
         "[dxf][kicad][pcb]") {
    TempPcbPath temp;
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(5, 5), 1.0,
                                                  0.5);
    insertA->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertA));
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(20, 0), Point2D(20, 20)},
                                                0.4));
    doc.addEntity(
        std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(30, 30), 0.7, 0.35));

    REQUIRE(writeKiCadPcb(doc, {}, temp.path.string()));

    Document loaded;
    std::string err;
    REQUIRE(readKiCadPcb(loaded, temp.path.string(), &err));

    int footprintCount = 0, trackSegments = 0, viaCount = 0;
    Point2D footprintPos;
    for (const Entity* e : loaded.entities()) {
        if (e->type() == EntityType::Insert) {
            const auto& insert = static_cast<const InsertEntity&>(*e);
            REQUIRE(insert.block() != nullptr);
            REQUIRE(insert.block()->pads.size() == rfp->pads.size());
            footprintPos = insert.position();
            ++footprintCount;
        } else if (e->type() == EntityType::Track) {
            const auto& track = static_cast<const TrackEntity&>(*e);
            REQUIRE(track.width() == Approx(0.4));
            ++trackSegments;
        } else if (e->type() == EntityType::Via) {
            const auto& via = static_cast<const ViaEntity&>(*e);
            REQUIRE(via.position().x == Approx(30.0));
            REQUIRE(via.position().y == Approx(30.0));
            REQUIRE(via.diameter() == Approx(0.7));
            REQUIRE(via.drillDiameter() == Approx(0.35));
            ++viaCount;
        }
    }
    REQUIRE(footprintCount == 1);
    REQUIRE(footprintPos.x == Approx(5.0));
    REQUIRE(footprintPos.y == Approx(5.0));
    // A 3-vertex track polyline is written as 2 discrete segments (real
    // KiCad's own PCB track representation -- see KiCadPcb.h).
    REQUIRE(trackSegments == 2);
    REQUIRE(viaCount == 1);
}

TEST_CASE("readKiCadPcb reports failure for a missing or malformed file", "[dxf][kicad][pcb]") {
    Document doc;
    std::string err;
    REQUIRE_FALSE(readKiCadPcb(doc, "/tmp/kumcad_does_not_exist.kicad_pcb", &err));
    REQUIRE_FALSE(err.empty());

    TempPcbPath temp;
    {
        std::ofstream out(temp.path);
        out << "(not_a_board 1 2 3)";
    }
    err.clear();
    REQUIRE_FALSE(readKiCadPcb(doc, temp.path.string(), &err));
    REQUIRE_FALSE(err.empty());
}
