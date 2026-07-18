#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Table.h"
#include "core/geometry/Text.h"
#include "core/geometry/Wipeout.h"
#include "core/io/DwgReader.h"
#include "core/io/DwgWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#ifdef LCAD_HAS_DWG
#include <dwg.h>
#include <dwg_api.h>
#include <strings.h>
#endif

using Catch::Approx;

namespace {

struct TempDwgPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_dwg_test_" + std::to_string(std::rand()) + ".dwg");
    ~TempDwgPath() { std::filesystem::remove(path); }
};

const lcad::Entity* findByType(const std::vector<lcad::Entity*>& entities, lcad::EntityType type) {
    for (const lcad::Entity* e : entities) {
        if (e->type() == type) return e;
    }
    return nullptr;
}

} // namespace

TEST_CASE("DWG write/read round-trips the core entity set", "[dwg]") {
    if (!lcad::dwgWriteSupportAvailable()) {
        SUCCEED("built without LibreDWG; DWG export not available");
        return;
    }

    TempDwgPath temp;

    lcad::Document doc;
    const lcad::LayerId walls = doc.addLayer("Walls", lcad::Color{200, 50, 50});
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), walls, lcad::Point2D(0, 0),
                                                     lcad::Point2D(100, 50)));
    doc.addEntity(
        std::make_unique<lcad::CircleEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(20, 20), 7.5));
    doc.addEntity(std::make_unique<lcad::ArcEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                    5.0, 0.0, M_PI / 2));
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}};
    doc.addEntity(std::make_unique<lcad::PolylineEntity>(doc.reserveEntityId(), walls, verts, true));
    doc.addEntity(std::make_unique<lcad::TextEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(5, 5),
                                                     "dwg out", 2.5));

    std::string error;
    int skipped = 0;
    REQUIRE(lcad::writeDwg(doc, temp.path.string(), &error, &skipped));
    REQUIRE(skipped == 0);

    lcad::Document loaded;
    REQUIRE(lcad::readDwg(loaded, temp.path.string(), &error));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 5);

    const auto* line = static_cast<const lcad::LineEntity*>(findByType(entities, lcad::EntityType::Line));
    REQUIRE(line);
    REQUIRE(line->end().x == Approx(100.0));
    REQUIRE(line->end().y == Approx(50.0));
    const lcad::Layer* lineLayer = loaded.findLayer(line->layer());
    REQUIRE(lineLayer);
    REQUIRE(lineLayer->name == "Walls");

    const auto* circle = static_cast<const lcad::CircleEntity*>(findByType(entities, lcad::EntityType::Circle));
    REQUIRE(circle);
    REQUIRE(circle->radius() == Approx(7.5));

    const auto* arc = static_cast<const lcad::ArcEntity*>(findByType(entities, lcad::EntityType::Arc));
    REQUIRE(arc);
    REQUIRE(arc->radius() == Approx(5.0));

    const auto* pl = static_cast<const lcad::PolylineEntity*>(findByType(entities, lcad::EntityType::Polyline));
    REQUIRE(pl);
    REQUIRE(pl->vertices().size() == 3);
    REQUIRE(pl->closed());

    const auto* text = static_cast<const lcad::TextEntity*>(findByType(entities, lcad::EntityType::Text));
    REQUIRE(text);
    REQUIRE(text->text() == "dwg out");
    REQUIRE(text->height() == Approx(2.5));
}

TEST_CASE("DWG export degrades a WIPEOUT to its own boundary as a closed LWPOLYLINE",
         "[dwg]") {
    // No dwg_add_WIPEOUT exists in LibreDWG at all -- writeDwg's own
    // documented fallback is to keep the boundary as visible geometry
    // (the masking behavior itself is real, disclosed lost geometry, not
    // silently dropped) -- proves the fallback round-trips as a real,
    // readable closed polyline rather than just "doesn't crash".
    if (!lcad::dwgWriteSupportAvailable()) {
        SUCCEED("built without LibreDWG; DWG export not available");
        return;
    }

    TempDwgPath temp;
    lcad::Document doc;
    const std::vector<lcad::Point2D> boundary{{0, 0}, {10, 0}, {10, 5}, {0, 5}};
    doc.addEntity(std::make_unique<lcad::WipeoutEntity>(doc.reserveEntityId(), doc.currentLayer(), boundary, true));

    std::string error;
    int skipped = 0;
    REQUIRE(lcad::writeDwg(doc, temp.path.string(), &error, &skipped));
    REQUIRE(skipped == 0);

    lcad::Document loaded;
    REQUIRE(lcad::readDwg(loaded, temp.path.string(), &error));

    const auto entities = loaded.entities();
    const auto* pl = static_cast<const lcad::PolylineEntity*>(findByType(entities, lcad::EntityType::Polyline));
    REQUIRE(pl);
    REQUIRE(pl->closed());
    REQUIRE(pl->vertices().size() == 4);
}

TEST_CASE("DWG export covers leaders, hatch boundaries, and table grids", "[dwg]") {
    if (!lcad::dwgWriteSupportAvailable()) {
        SUCCEED("built without LibreDWG; DWG export not available");
        return;
    }

    TempDwgPath temp;
    lcad::Document doc;

    std::vector<lcad::Point2D> leaderPts{{0, 0}, {5, 5}, {10, 5}};
    doc.addEntity(std::make_unique<lcad::LeaderEntity>(doc.reserveEntityId(), doc.currentLayer(), leaderPts, 1.25));

    std::vector<lcad::Point2D> tri{{0, 0}, {10, 0}, {5, 8}};
    doc.addEntity(std::make_unique<lcad::HatchEntity>(doc.reserveEntityId(), doc.currentLayer(), tri));

    std::vector<double> rowHeights{1.0, 1.0};
    std::vector<double> colWidths{2.0, 2.0};
    std::vector<std::string> cells{"A1", "B1", "", "B2"};
    doc.addEntity(std::make_unique<lcad::TableEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(20, 20),
                                                       rowHeights, colWidths, cells, 1.0));

    std::string error;
    int skipped = 0;
    REQUIRE(lcad::writeDwg(doc, temp.path.string(), &error, &skipped));
    REQUIRE(skipped == 0);

    lcad::Document loaded;
    REQUIRE(lcad::readDwg(loaded, temp.path.string(), &error));
    const auto entities = loaded.entities();

    const auto* leader = static_cast<const lcad::LeaderEntity*>(findByType(entities, lcad::EntityType::Leader));
    REQUIRE(leader);
    REQUIRE(leader->points().size() == 3);
    REQUIRE(leader->points()[2].x == Approx(10.0));

    int closedTriangleCount = 0;
    int gridLineCount = 0;
    int textCount = 0;
    for (const lcad::Entity* e : entities) {
        if (e->type() == lcad::EntityType::Polyline) {
            const auto* p = static_cast<const lcad::PolylineEntity*>(e);
            if (p->closed() && p->vertices().size() == 3) ++closedTriangleCount;
            if (!p->closed() && p->vertices().size() == 2) ++gridLineCount;
        } else if (e->type() == lcad::EntityType::Text) {
            ++textCount;
        }
    }
    REQUIRE(closedTriangleCount >= 1);  // the exploded hatch boundary
    REQUIRE(gridLineCount >= 6);        // table: 3 horizontal + 3 vertical grid lines
    REQUIRE(textCount == 3);            // "A1", "B1", "B2" (the empty cell is skipped)
}

TEST_CASE("DWG export writes real hatch fill patterns and attribute definitions", "[dwg]") {
    if (!lcad::dwgWriteSupportAvailable()) {
        SUCCEED("built without LibreDWG; DWG export not available");
        return;
    }

    TempDwgPath temp;
    lcad::Document doc;

    std::vector<lcad::Point2D> tri{{0, 0}, {10, 0}, {5, 8}};
    doc.addEntity(std::make_unique<lcad::HatchEntity>(doc.reserveEntityId(), doc.currentLayer(), tri,
                                                       lcad::HatchPattern::Ansi31, 1.0, 0.0));

    std::vector<lcad::Point2D> solidTri{{20, 0}, {30, 0}, {25, 8}};
    doc.addEntity(std::make_unique<lcad::HatchEntity>(doc.reserveEntityId(), doc.currentLayer(), solidTri));

    std::vector<std::unique_ptr<lcad::Entity>> blockEntities;
    // A LINE precedes the ATTDEF here on purpose: dwg_add_ATTDEF only
    // splices correctly into a still-empty owned-entity chain, so a block
    // with prior geometry (the common attributed-block shape) is the
    // regression case worth locking in, not just an attdef-only block.
    blockEntities.push_back(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                                lcad::Point2D(-1, -1), lcad::Point2D(1, 1)));
    blockEntities.push_back(std::make_unique<lcad::AttDefEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                                  lcad::Point2D(0, -1), "PARTNO", "Part number?",
                                                                  "DEFAULT", 1.0));
    const lcad::BlockDefinition* block = doc.addBlock("PARTBLK", std::move(blockEntities));
    auto insert = std::make_unique<lcad::InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block,
                                                        lcad::Point2D(50, 50), 1.0, 0.0);
    insert->setAttribute("PARTNO", "AB-123");
    doc.addEntity(std::move(insert));

    std::string error;
    int skipped = 0;
    REQUIRE(lcad::writeDwg(doc, temp.path.string(), &error, &skipped));
    REQUIRE(skipped == 0);

    lcad::Document loaded;
    REQUIRE(lcad::readDwg(loaded, temp.path.string(), &error));
    const auto entities = loaded.entities();

    int patternHatchCount = 0;
    int solidHatchCount = 0;
    for (const lcad::Entity* e : entities) {
        if (e->type() != lcad::EntityType::Hatch) continue;
        const auto* h = static_cast<const lcad::HatchEntity*>(e);
        if (h->pattern() == lcad::HatchPattern::Ansi31) ++patternHatchCount;
        if (h->pattern() == lcad::HatchPattern::Solid) ++solidHatchCount;
    }
    REQUIRE(patternHatchCount == 1); // real ANSI31 fill, not an outline-only shape
    REQUIRE(solidHatchCount == 1);

    // The block definition carries the ATTDEF itself; the standalone
    // top-level attribute value round-trips as an ATTRIB understood by
    // LibreDWG's own reader (verified against dwgread separately), but
    // KumCAD's DwgReader doesn't yet resolve INSERT sub-entity chains, so
    // only the ATTDEF inside the block definition is checked here.
    const lcad::BlockDefinition* loadedBlock = loaded.findBlock("PARTBLK");
    REQUIRE(loadedBlock);
    REQUIRE(loadedBlock->entities.size() == 2);
    bool foundLine = false;
    bool foundAttDef = false;
    for (const auto& child : loadedBlock->entities) {
        if (child->type() == lcad::EntityType::Line) foundLine = true;
        if (child->type() != lcad::EntityType::AttDef) continue;
        const auto* attdef = static_cast<const lcad::AttDefEntity*>(child.get());
        if (attdef->tag() == "PARTNO" && attdef->defaultValue() == "DEFAULT") foundAttDef = true;
    }
    REQUIRE(foundLine);
    REQUIRE(foundAttDef);
}

TEST_CASE("DWG export writes every paper-space layout, not just the first", "[dwg]") {
    if (!lcad::dwgWriteSupportAvailable()) {
        SUCCEED("built without LibreDWG; DWG export not available");
        return;
    }

    TempDwgPath temp;
    lcad::Document doc;

    doc.layouts()[0].name = "Layout1";
    doc.setActiveSpace(0);
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                      lcad::Point2D(5, 5)));

    lcad::Layout second;
    second.name = "Layout2";
    doc.layouts().push_back(second);
    doc.setActiveSpace(1);
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(1, 1),
                                                      lcad::Point2D(9, 9)));
    doc.setActiveSpace(-1);

    std::string error;
    int skipped = 0;
    REQUIRE(lcad::writeDwg(doc, temp.path.string(), &error, &skipped));
    REQUIRE(skipped == 0);

#ifdef LCAD_HAS_DWG
    // readDwg() only reconstructs model space today, so the second layout is
    // verified against LibreDWG's own reader directly instead.
    Dwg_Data dwg;
    std::memset(&dwg, 0, sizeof(dwg));
    REQUIRE(dwg_read_file(temp.path.string().c_str(), &dwg) < DWG_ERR_CRITICAL);

    int layoutObjectCount = 0;
    int paperSpaceBlockCount = 0;
    bool foundSecondLine = false;
    for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
        Dwg_Object* obj = &dwg.object[i];
        if (obj->fixedtype == DWG_TYPE_LAYOUT) ++layoutObjectCount;
        if (obj->fixedtype == DWG_TYPE_BLOCK_HEADER && obj->supertype == DWG_SUPERTYPE_OBJECT) {
            int err = 0;
            char* name = dwg_obj_table_get_name(obj, &err);
            if (!err && name && strncasecmp(name, "*Paper_Space", 12) == 0) ++paperSpaceBlockCount;
        }
        if (obj->fixedtype == DWG_TYPE_LINE) {
            const auto* line = obj->tio.entity->tio.LINE;
            if (line->start.x == 1.0 && line->start.y == 1.0 && line->end.x == 9.0) foundSecondLine = true;
        }
    }
    dwg_free(&dwg);

    REQUIRE(layoutObjectCount == 3);      // Model, Layout1, Layout2
    REQUIRE(paperSpaceBlockCount == 2);   // one paper-space block per layout
    REQUIRE(foundSecondLine);             // Layout2's own line, not just Layout1's
#endif
}
