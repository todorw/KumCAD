#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/schematic/Erc.h"
#include "core/schematic/Netlist.h"
#include "core/schematic/SymbolLibrary.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;

TEST_CASE("registerBuiltinSymbols adds a crystal, fuse, battery, and op-amp symbol", "[schematic][symbols]") {
    Document doc;
    registerBuiltinSymbols(doc);

    const BlockDefinition* xtal = doc.findBlock("XTAL");
    REQUIRE(xtal);
    REQUIRE(xtal->pins.size() == 2);

    const BlockDefinition* fuse = doc.findBlock("F");
    REQUIRE(fuse);
    REQUIRE(fuse->pins.size() == 2);

    const BlockDefinition* bat = doc.findBlock("BAT");
    REQUIRE(bat);
    REQUIRE(bat->pins.size() == 2);
    REQUIRE(bat->pins[0].electricalType == PinElectricalType::PowerOutput);
    REQUIRE(bat->pins[1].electricalType == PinElectricalType::PowerOutput);

    const BlockDefinition* opamp = doc.findBlock("OPAMP");
    REQUIRE(opamp);
    REQUIRE(opamp->pins.size() == 5);
    int inputCount = 0, outputCount = 0, powerCount = 0;
    for (const Pin& p : opamp->pins) {
        if (p.electricalType == PinElectricalType::Input) ++inputCount;
        if (p.electricalType == PinElectricalType::Output) ++outputCount;
        if (p.electricalType == PinElectricalType::Power) ++powerCount;
    }
    REQUIRE(inputCount == 2);
    REQUIRE(outputCount == 1);
    REQUIRE(powerCount == 2);
}

TEST_CASE("registerBuiltinSymbols adds real GND/VCC power-flag symbols with a PowerOutput pin",
         "[schematic][symbols]") {
    Document doc;
    registerBuiltinSymbols(doc);

    const BlockDefinition* gnd = doc.findBlock("GND");
    REQUIRE(gnd);
    REQUIRE(gnd->pins.size() == 1);
    REQUIRE(gnd->pins[0].electricalType == PinElectricalType::PowerOutput);
    REQUIRE(gnd->pins[0].name == "GND");

    const BlockDefinition* vcc = doc.findBlock("VCC");
    REQUIRE(vcc);
    REQUIRE(vcc->pins.size() == 1);
    REQUIRE(vcc->pins[0].electricalType == PinElectricalType::PowerOutput);

    // Pure power-flag annotations, not physical parts -- no matching
    // footprint, same reasoning as the simulation-only V/I sources.
    REQUIRE_FALSE(doc.findBlock("GND_FP"));
    REQUIRE_FALSE(doc.findBlock("VCC_FP"));
}

TEST_CASE("registerBuiltinSymbols gives XTAL/F/BAT/OPAMP real matching footprints", "[schematic][symbols]") {
    Document doc;
    registerBuiltinSymbols(doc);

    const BlockDefinition* xtalFp = doc.findBlock("XTAL_FP");
    REQUIRE(xtalFp);
    REQUIRE(xtalFp->isFootprint());
    REQUIRE(xtalFp->pads.size() == 2);

    const BlockDefinition* fuseFp = doc.findBlock("F_FP");
    REQUIRE(fuseFp);
    REQUIRE(fuseFp->pads.size() == 2);

    const BlockDefinition* batFp = doc.findBlock("BAT_FP");
    REQUIRE(batFp);
    REQUIRE(batFp->pads.size() == 2);

    const BlockDefinition* opampFp = doc.findBlock("OPAMP_FP");
    REQUIRE(opampFp);
    REQUIRE(opampFp->pads.size() == 8);
}

TEST_CASE("A GND symbol satisfies ERC's undriven-power-pin check the way V/I sources already do",
         "[schematic][symbols][erc]") {
    Document doc;
    registerBuiltinSymbols(doc);

    const BlockDefinition* opamp = doc.findBlock("OPAMP");
    const BlockDefinition* gnd = doc.findBlock("GND");
    REQUIRE(opamp);
    REQUIRE(gnd);

    auto opampInsert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), opamp, Point2D(0, 0));
    doc.addEntity(std::move(opampInsert));
    // GND's own pin (world position matches its "position" field, (10,5))
    // placed so it lands exactly on the op-amp's own V- pin (world (12,-15)).
    auto gndInsert =
        std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), gnd, Point2D(2, -20));
    doc.addEntity(std::move(gndInsert));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);

    // The op-amp's V- pin (a Power/load pin) must NOT be flagged as
    // undriven now that a real PowerOutput (GND) pin shares its net.
    const bool vMinusFlagged = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.message.find("V-") != std::string::npos;
    });
    REQUIRE_FALSE(vMinusFlagged);
}
