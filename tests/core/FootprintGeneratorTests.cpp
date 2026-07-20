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

TEST_CASE("generateChipPassiveFootprint builds a 2-pad SMD passive with pads straddling center",
         "[pcb][footprintgen]") {
    Document doc;
    ChipPassiveParams params; // 0603-ish defaults
    REQUIRE(generateChipPassiveFootprint(doc, "R_0603", params));
    const BlockDefinition* block = doc.findBlock("R_0603");
    REQUIRE(block);
    REQUIRE(block->isFootprint());
    REQUIRE(block->pads.size() == 2);
    REQUIRE(block->pads[0].number == "1");
    REQUIRE(block->pads[1].number == "2");
    REQUIRE(block->pads[0].position.x < 0.0);
    REQUIRE(block->pads[1].position.x > 0.0);
    REQUIRE(block->pads[0].position.x == Approx(-block->pads[1].position.x));
    REQUIRE(block->pads[0].drillDiameter == Approx(0.0)); // SMD, not through-hole
}

TEST_CASE("generateChipPassiveFootprint rejects a non-positive dimension", "[pcb][footprintgen]") {
    Document doc;
    ChipPassiveParams bad;
    bad.padSpacing = 0.0;
    REQUIRE_FALSE(generateChipPassiveFootprint(doc, "BadChip", bad));
}

TEST_CASE("generateSot23Footprint builds 3 pads: 2 on the bottom straddling center, 1 centered on top",
         "[pcb][footprintgen]") {
    Document doc;
    SotParams params;
    REQUIRE(generateSot23Footprint(doc, "SOT23", params));
    const BlockDefinition* block = doc.findBlock("SOT23");
    REQUIRE(block);
    REQUIRE(block->pads.size() == 3);
    REQUIRE(block->pads[0].position.y == Approx(block->pads[1].position.y)); // pins 1,2 share the bottom side
    REQUIRE(block->pads[0].position.x == Approx(-block->pads[1].position.x));
    REQUIRE(block->pads[2].position.x == Approx(0.0)); // pin 3 centered
    REQUIRE(block->pads[2].position.y > block->pads[0].position.y); // pin 3 on the opposite side
}

TEST_CASE("generateSot223Footprint builds 3 small pads plus 1 wide tab pad", "[pcb][footprintgen]") {
    Document doc;
    Sot223Params params;
    REQUIRE(generateSot223Footprint(doc, "SOT223", params));
    const BlockDefinition* block = doc.findBlock("SOT223");
    REQUIRE(block);
    REQUIRE(block->pads.size() == 4);
    REQUIRE(block->pads[3].number == "4");
    REQUIRE(block->pads[3].width > block->pads[0].width); // the tab is much wider than a signal pin
    REQUIRE(block->pads[3].position.y > block->pads[0].position.y); // tab on the opposite side from pins 1-3
}

TEST_CASE("generateBgaFootprint builds a rows x cols grid with real JEDEC ball naming", "[pcb][footprintgen]") {
    Document doc;
    BgaParams params;
    params.rows = 4;
    params.cols = 4;
    params.pitch = 0.8;
    REQUIRE(generateBgaFootprint(doc, "BGA16", params));
    const BlockDefinition* block = doc.findBlock("BGA16");
    REQUIRE(block);
    REQUIRE(block->pads.size() == 16);
    REQUIRE(block->pads[0].number == "A1");
    REQUIRE(block->pads[3].number == "A4");
    REQUIRE(block->pads[15].number == "D4");
    // JEDEC skips I, O, Q, S, X, Z -- a 9-row grid's 9th row is J, not I.
    BgaParams tall;
    tall.rows = 9;
    tall.cols = 1;
    REQUIRE(generateBgaFootprint(doc, "BGA_TALL", tall));
    const BlockDefinition* tallBlock = doc.findBlock("BGA_TALL");
    REQUIRE(tallBlock->pads[8].number == "J1");
}

TEST_CASE("generateBgaFootprint rejects a grid too tall for the JEDEC letter set", "[pcb][footprintgen]") {
    Document doc;
    BgaParams tooTall;
    tooTall.rows = 25; // only 20 usable JEDEC letters
    tooTall.cols = 1;
    REQUIRE_FALSE(generateBgaFootprint(doc, "TooTall", tooTall));
}

TEST_CASE("generateMountingHoleFootprint builds a single unnumbered plated-through pad", "[pcb][footprintgen]") {
    Document doc;
    REQUIRE(generateMountingHoleFootprint(doc, "MountingHole_M3", 3.2, 6.0));
    const BlockDefinition* block = doc.findBlock("MountingHole_M3");
    REQUIRE(block);
    REQUIRE(block->pads.size() == 1);
    REQUIRE(block->pads[0].number.empty()); // not part of any net
    REQUIRE(block->pads[0].drillDiameter == Approx(3.2));
    REQUIRE(block->pads[0].width == Approx(6.0));
}

TEST_CASE("generateFiducialFootprint builds a single unnumbered SMD pad with no drill", "[pcb][footprintgen]") {
    Document doc;
    REQUIRE(generateFiducialFootprint(doc, "Fiducial_1mm", 1.0));
    const BlockDefinition* block = doc.findBlock("Fiducial_1mm");
    REQUIRE(block);
    REQUIRE(block->pads.size() == 1);
    REQUIRE(block->pads[0].number.empty());
    REQUIRE(block->pads[0].drillDiameter == Approx(0.0));
    REQUIRE(block->pads[0].width == Approx(1.0));
}

TEST_CASE("Footprint generators reject a name that's already registered", "[pcb][footprintgen]") {
    Document doc;
    REQUIRE(generateFiducialFootprint(doc, "Dup", 1.0));
    REQUIRE_FALSE(generateFiducialFootprint(doc, "Dup", 1.0));
    REQUIRE_FALSE(generateMountingHoleFootprint(doc, "Dup", 3.2, 6.0));
    REQUIRE_FALSE(generateChipPassiveFootprint(doc, "Dup", ChipPassiveParams{}));
    REQUIRE_FALSE(generateSot23Footprint(doc, "Dup", SotParams{}));
    REQUIRE_FALSE(generateSot223Footprint(doc, "Dup", Sot223Params{}));
    REQUIRE_FALSE(generateBgaFootprint(doc, "Dup", BgaParams{}));
}
