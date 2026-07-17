#include "core/document/Document.h"
#include "core/pcb/FootprintGenerator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("generateGullWingFootprint builds a QFP-style footprint with correct counterclockwise pin placement",
         "[pcb][footprintgen]") {
    Document doc;
    GullWingParams params;
    params.pinCount = 8;
    params.sideCount = 4;
    params.pitch = 1.0;
    params.bodyWidth = 4.0;
    params.bodyLength = 4.0;
    params.padWidth = 0.4;
    params.padLength = 1.0;

    REQUIRE(generateGullWingFootprint(doc, "QFP8", params));
    const BlockDefinition* block = doc.findBlock("QFP8");
    REQUIRE(block);
    REQUIRE(block->isFootprint());
    REQUIRE(block->pads.size() == 8);

    // Pin 1: left side, top -- pin 8: top side, left end (just short of
    // wrapping back around to pin 1, the standard QFP corner gap).
    REQUIRE(block->pads[0].number == "1");
    REQUIRE(block->pads[0].position.x == Approx(-2.5));
    REQUIRE(block->pads[0].position.y == Approx(0.5));
    REQUIRE(block->pads[7].number == "8");
    REQUIRE(block->pads[7].position.x == Approx(-0.5));
    REQUIRE(block->pads[7].position.y == Approx(2.5));

    // Pin 3: first bottom-side pad -- width/height swapped vs. the
    // left/right pads (a bottom pad is wide in X, narrow in Y).
    REQUIRE(block->pads[2].number == "3");
    REQUIRE(block->pads[2].position.x == Approx(-0.5));
    REQUIRE(block->pads[2].position.y == Approx(-2.5));
    REQUIRE(block->pads[2].width == Approx(params.padWidth));
    REQUIRE(block->pads[2].height == Approx(params.padLength));
    REQUIRE(block->pads[0].width == Approx(params.padLength));
    REQUIRE(block->pads[0].height == Approx(params.padWidth));
}

TEST_CASE("generateGullWingFootprint builds a SOIC-style (2-side) footprint", "[pcb][footprintgen]") {
    Document doc;
    GullWingParams params;
    params.pinCount = 4;
    params.sideCount = 2;
    params.pitch = 1.0;
    params.bodyWidth = 4.0;
    params.bodyLength = 4.0;
    params.padWidth = 0.4;
    params.padLength = 1.0;

    REQUIRE(generateGullWingFootprint(doc, "SOIC4", params));
    const BlockDefinition* block = doc.findBlock("SOIC4");
    REQUIRE(block);
    REQUIRE(block->pads.size() == 4);

    REQUIRE(block->pads[0].position.x == Approx(-2.5));
    REQUIRE(block->pads[0].position.y == Approx(0.5));
    REQUIRE(block->pads[1].position.x == Approx(-2.5));
    REQUIRE(block->pads[1].position.y == Approx(-0.5));
    REQUIRE(block->pads[2].position.x == Approx(2.5));
    REQUIRE(block->pads[2].position.y == Approx(-0.5));
    REQUIRE(block->pads[3].position.x == Approx(2.5));
    REQUIRE(block->pads[3].position.y == Approx(0.5));
}

TEST_CASE("generateGullWingFootprint rejects invalid parameters and duplicate names", "[pcb][footprintgen]") {
    Document doc;
    GullWingParams bad;
    bad.pinCount = 10;
    bad.sideCount = 3; // only 2 or 4 supported
    REQUIRE_FALSE(generateGullWingFootprint(doc, "Bad1", bad));

    GullWingParams mismatched;
    mismatched.pinCount = 10;
    mismatched.sideCount = 4; // doesn't divide evenly
    REQUIRE_FALSE(generateGullWingFootprint(doc, "Bad2", mismatched));

    GullWingParams ok;
    ok.pinCount = 8;
    ok.sideCount = 4;
    REQUIRE(generateGullWingFootprint(doc, "QFP_Dup", ok));
    REQUIRE_FALSE(generateGullWingFootprint(doc, "QFP_Dup", ok)); // name already registered
}

TEST_CASE("generatePinHeaderFootprint builds single- and dual-row through-hole headers", "[pcb][footprintgen]") {
    Document doc;
    PinHeaderParams single;
    single.pinCount = 4;
    single.rowCount = 1;
    single.pitch = 2.54;
    REQUIRE(generatePinHeaderFootprint(doc, "HDR1X4", single));

    const BlockDefinition* block = doc.findBlock("HDR1X4");
    REQUIRE(block);
    REQUIRE(block->pads.size() == 4);
    for (std::size_t i = 0; i < block->pads.size(); ++i) {
        REQUIRE(block->pads[i].position.x == Approx(0.0));
        REQUIRE(block->pads[i].position.y == Approx(static_cast<double>(i) * 2.54));
        REQUIRE(block->pads[i].drillDiameter > 0.0); // through-hole
    }

    PinHeaderParams dual;
    dual.pinCount = 3;
    dual.rowCount = 2;
    dual.pitch = 2.54;
    REQUIRE(generatePinHeaderFootprint(doc, "HDR2X3", dual));
    const BlockDefinition* dualBlock = doc.findBlock("HDR2X3");
    REQUIRE(dualBlock);
    REQUIRE(dualBlock->pads.size() == 6);
    REQUIRE(dualBlock->pads[0].position.x == Approx(-1.27));
    REQUIRE(dualBlock->pads[3].position.x == Approx(1.27));
}

TEST_CASE("generatePinHeaderFootprint rejects invalid parameters", "[pcb][footprintgen]") {
    Document doc;
    PinHeaderParams bad;
    bad.rowCount = 3; // only 1 or 2 supported
    REQUIRE_FALSE(generatePinHeaderFootprint(doc, "BadHeader", bad));
}
