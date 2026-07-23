#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/geometry/Wire.h"
#include "core/schematic/Erc.h"
#include "core/schematic/Netlist.h"
#include "core/schematic/SymbolLibrary.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;

namespace {

// A minimal two-pin symbol (like a resistor): pin "1" at (0,0), pin "2" at
// (10,0), body geometry irrelevant to netlist computation so left empty.
const BlockDefinition* addTwoPinSymbol(Document& doc, const std::string& name) {
    doc.addBlock(name, {});
    BlockDefinition* block = doc.findBlock(name);
    block->pins.push_back(Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(-5, 0)});
    block->pins.push_back(Pin{"2", "2", PinElectricalType::Passive, Point2D(10, 0), Point2D(15, 0)});
    return block;
}

} // namespace

TEST_CASE("computeNets joins two symbol pins through one wire", "[schematic][netlist]") {
    Document doc;
    const BlockDefinition* r1 = addTwoPinSymbol(doc, "R");
    const BlockDefinition* r2 = addTwoPinSymbol(doc, "R2");

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r1, Point2D(0, 0));
    const EntityId idA = insertA->id();
    doc.addEntity(std::move(insertA));

    // r2's pin "1" (block-local (0,0)) lands at world (100,0) via the offset below.
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r2, Point2D(100, 0));
    const EntityId idB = insertB->id();
    doc.addEntity(std::move(insertB));

    // Wire from R1 pin "2" (world (10,0)) to R2 pin "1" (world (100,0)).
    doc.addEntity(
        std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                      std::vector<Point2D>{Point2D(10, 0), Point2D(100, 0)}));

    const std::vector<Net> nets = computeNets(doc);

    // One net containing both wired pins; R1's pin "1" and R2's pin "2" are
    // each their own unconnected singleton net.
    REQUIRE(nets.size() == 3);
    const auto wiredNet = std::find_if(nets.begin(), nets.end(), [](const Net& n) { return n.pins.size() == 2; });
    REQUIRE(wiredNet != nets.end());
    const bool hasA2 = std::any_of(wiredNet->pins.begin(), wiredNet->pins.end(),
                                    [&](const NetPin& p) { return p.insertId == idA && p.pinNumber == "2"; });
    const bool hasB1 = std::any_of(wiredNet->pins.begin(), wiredNet->pins.end(),
                                    [&](const NetPin& p) { return p.insertId == idB && p.pinNumber == "1"; });
    REQUIRE(hasA2);
    REQUIRE(hasB1);
}

TEST_CASE("computeNets keeps crossing wires separate without a junction", "[schematic][netlist]") {
    Document doc;
    // Two wires crossing at (5,5): one horizontal, one vertical, neither
    // sharing an endpoint at the crossing point -- must NOT be one net.
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(0, 5), Point2D(10, 5)}));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(5, 0), Point2D(5, 10)}));

    const BlockDefinition* r = addTwoPinSymbol(doc, "R");
    // Pin "1" sits at the crossing point on wire 1's endpoint... instead,
    // attach two symbols at true endpoints of each wire so each wire forms
    // its own net, proving the crossing point did not merge them.
    auto insertLeft = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 5));
    const EntityId leftId = insertLeft->id();
    doc.addEntity(std::move(insertLeft));
    auto insertBottom = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(5, -10));
    const EntityId bottomId = insertBottom->id();
    doc.addEntity(std::move(insertBottom));

    const std::vector<Net> nets = computeNets(doc);
    for (const Net& net : nets) {
        const bool hasLeft = std::any_of(net.pins.begin(), net.pins.end(),
                                          [&](const NetPin& p) { return p.insertId == leftId; });
        const bool hasBottom = std::any_of(net.pins.begin(), net.pins.end(),
                                            [&](const NetPin& p) { return p.insertId == bottomId; });
        REQUIRE_FALSE((hasLeft && hasBottom));
    }
}

TEST_CASE("computeNets merges crossing wires when a junction marks the tap", "[schematic][netlist]") {
    Document doc;
    // A horizontal wire with an interior vertex at (5,5) (the tap point),
    // and a vertical wire ending exactly there, joined by a Junction.
    doc.addEntity(std::make_unique<WireEntity>(
        doc.reserveEntityId(), doc.currentLayer(),
        std::vector<Point2D>{Point2D(0, 5), Point2D(5, 5), Point2D(10, 5)}));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(5, 5), Point2D(5, 15)}));
    doc.addEntity(std::make_unique<JunctionEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(5, 5)));

    const BlockDefinition* r = addTwoPinSymbol(doc, "R");
    auto insertLeft = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 5));
    const EntityId leftId = insertLeft->id();
    doc.addEntity(std::move(insertLeft));
    auto insertTop = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(5, 15));
    const EntityId topId = insertTop->id();
    doc.addEntity(std::move(insertTop));

    const std::vector<Net> nets = computeNets(doc);
    const auto merged = std::find_if(nets.begin(), nets.end(), [&](const Net& n) {
        const bool hasLeft = std::any_of(n.pins.begin(), n.pins.end(), [&](const NetPin& p) { return p.insertId == leftId; });
        const bool hasTop = std::any_of(n.pins.begin(), n.pins.end(), [&](const NetPin& p) { return p.insertId == topId; });
        return hasLeft && hasTop;
    });
    REQUIRE(merged != nets.end());
}

TEST_CASE("computeNets names a net after a touching NetLabel", "[schematic][netlist]") {
    Document doc;
    const BlockDefinition* r = addTwoPinSymbol(doc, "R");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 0));
    doc.addEntity(std::move(insert));
    doc.addEntity(std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0), "VCC"));

    const std::vector<Net> nets = computeNets(doc);
    const auto named = std::find_if(nets.begin(), nets.end(), [](const Net& n) { return n.name == "VCC"; });
    REQUIRE(named != nets.end());
}

TEST_CASE("computeNets joins two otherwise-disconnected groups via same-named labels (hierarchical connectivity)",
          "[schematic][netlist][sheets]") {
    Document doc;
    const BlockDefinition* r1 = addTwoPinSymbol(doc, "R");
    const BlockDefinition* r2 = addTwoPinSymbol(doc, "R2");

    // Two symbols far apart, on what would be different "sheets" in
    // practice -- nothing physically connects them.
    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r1, Point2D(0, 0));
    const EntityId idA = insertA->id();
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r2, Point2D(1000, 1000));
    const EntityId idB = insertB->id();
    doc.addEntity(std::move(insertB));

    // A "VCC" label touching each symbol's pin "1" -- same name, nowhere
    // near each other -- should still be one net.
    doc.addEntity(std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0), "VCC"));
    doc.addEntity(
        std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(1000, 1000), "VCC"));

    const std::vector<Net> nets = computeNets(doc);
    const auto vcc = std::find_if(nets.begin(), nets.end(), [](const Net& n) { return n.name == "VCC"; });
    REQUIRE(vcc != nets.end());
    REQUIRE(vcc->pins.size() == 2);
    const bool hasA1 = std::any_of(vcc->pins.begin(), vcc->pins.end(),
                                    [&](const NetPin& p) { return p.insertId == idA && p.pinNumber == "1"; });
    const bool hasB1 = std::any_of(vcc->pins.begin(), vcc->pins.end(),
                                    [&](const NetPin& p) { return p.insertId == idB && p.pinNumber == "1"; });
    REQUIRE(hasA1);
    REQUIRE(hasB1);
}

TEST_CASE("computeNets does not connect labels with different names", "[schematic][netlist][sheets]") {
    Document doc;
    const BlockDefinition* r1 = addTwoPinSymbol(doc, "R");
    const BlockDefinition* r2 = addTwoPinSymbol(doc, "R2");
    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r1, Point2D(0, 0));
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r2, Point2D(1000, 1000));
    doc.addEntity(std::move(insertB));
    doc.addEntity(std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(0, 0), "VCC"));
    doc.addEntity(
        std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(1000, 1000), "GND"));

    const std::vector<Net> nets = computeNets(doc);
    const auto vcc = std::find_if(nets.begin(), nets.end(), [](const Net& n) { return n.name == "VCC"; });
    const auto gnd = std::find_if(nets.begin(), nets.end(), [](const Net& n) { return n.name == "GND"; });
    REQUIRE(vcc != nets.end());
    REQUIRE(gnd != nets.end());
    REQUIRE(vcc->pins.size() == 1);
    REQUIRE(gnd->pins.size() == 1);
}

TEST_CASE("runErc flags an unconnected pin but not one marked NoConnect", "[schematic][erc]") {
    Document doc;
    const BlockDefinition* r = addTwoPinSymbol(doc, "R");

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 0));
    doc.addEntity(std::move(insertA)); // both pins unconnected

    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(100, 0));
    doc.addEntity(std::move(insertB));
    // Mark R2's pin "1" (world (100,0)) as intentionally unconnected.
    doc.addEntity(std::make_unique<NoConnectEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(100, 0)));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);

    // R1 pin 1 (0,0), R1 pin 2 (10,0), R2 pin 2 (110,0) unconnected; R2 pin 1 excused by NoConnect.
    REQUIRE(issues.size() == 3);
    for (const ErcIssue& issue : issues) REQUIRE(issue.severity == ErcIssue::Severity::Warning);
}

TEST_CASE("runErc flags multiple Output pins tied to the same net", "[schematic][erc]") {
    Document doc;
    doc.addBlock("GATE", {});
    BlockDefinition* gate = doc.findBlock("GATE");
    gate->pins.push_back(Pin{"OUT", "1", PinElectricalType::Output, Point2D(0, 0), Point2D(-5, 0)});

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), gate, Point2D(0, 0));
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), gate, Point2D(0, 20));
    doc.addEntity(std::move(insertB));
    // Wire both OUT pins (world (0,0) and (0,20)) together.
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(0, 20)}));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);

    const bool hasConflict = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.severity == ErcIssue::Severity::Error && issue.message.find("Output pins") != std::string::npos;
    });
    REQUIRE(hasConflict);
}

TEST_CASE("runErc flags a Power pin wired to another Power pin with nothing actually driving the net",
         "[schematic][erc]") {
    Document doc;
    doc.addBlock("IC", {});
    BlockDefinition* ic = doc.findBlock("IC");
    ic->pins.push_back(Pin{"VCC", "1", PinElectricalType::Power, Point2D(0, 0), Point2D(-5, 0)});

    doc.addBlock("RELAY", {});
    BlockDefinition* relay = doc.findBlock("RELAY");
    relay->pins.push_back(Pin{"A1", "1", PinElectricalType::Power, Point2D(0, 0), Point2D(-5, 0)});

    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), ic, Point2D(0, 0)));
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), relay, Point2D(0, 20)));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(0, 0), Point2D(0, 20)}));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);

    const bool hasUndriven = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.message.find("not driven") != std::string::npos;
    });
    REQUIRE(hasUndriven);
}

TEST_CASE("runErc does NOT flag a Power pin when a PowerOutput or Output pin is on the same net",
         "[schematic][erc]") {
    Document doc;
    doc.addBlock("IC", {});
    BlockDefinition* ic = doc.findBlock("IC");
    ic->pins.push_back(Pin{"VCC", "1", PinElectricalType::Power, Point2D(0, 0), Point2D(-5, 0)});

    doc.addBlock("BATTERY", {});
    BlockDefinition* battery = doc.findBlock("BATTERY");
    battery->pins.push_back(Pin{"+", "1", PinElectricalType::PowerOutput, Point2D(0, 0), Point2D(-5, 0)});

    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), ic, Point2D(0, 0)));
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), battery, Point2D(0, 20)));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(0, 0), Point2D(0, 20)}));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);

    const bool hasUndriven = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.message.find("not driven") != std::string::npos;
    });
    REQUIRE_FALSE(hasUndriven);
}

TEST_CASE("runErc does not double-report a lone Power pin: only the plain \"unconnected\" warning fires",
         "[schematic][erc]") {
    Document doc;
    doc.addBlock("IC", {});
    BlockDefinition* ic = doc.findBlock("IC");
    ic->pins.push_back(Pin{"VCC", "1", PinElectricalType::Power, Point2D(0, 0), Point2D(-5, 0)});
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), ic, Point2D(0, 0)));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);

    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].message.find("unconnected") != std::string::npos);
}

TEST_CASE("runErc flags an Output tied to a PowerOutput as a driver conflict (not just Output-Output)",
         "[schematic][erc]") {
    Document doc;
    doc.addBlock("GATE", {});
    BlockDefinition* gate = doc.findBlock("GATE");
    gate->pins.push_back(Pin{"OUT", "1", PinElectricalType::Output, Point2D(0, 0), Point2D(-5, 0)});

    doc.addBlock("REG", {});
    BlockDefinition* reg = doc.findBlock("REG");
    reg->pins.push_back(Pin{"VOUT", "1", PinElectricalType::PowerOutput, Point2D(0, 0), Point2D(-5, 0)});

    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), gate, Point2D(0, 0)));
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), reg, Point2D(0, 20)));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(0, 0), Point2D(0, 20)}));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);
    const bool hasConflict = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.severity == ErcIssue::Severity::Error && issue.message.find("driving pins") != std::string::npos;
    });
    REQUIRE(hasConflict);
}

TEST_CASE("runErc warns about a Bidirectional pin sharing a net with an Output driver", "[schematic][erc]") {
    Document doc;
    doc.addBlock("GATE", {});
    BlockDefinition* gate = doc.findBlock("GATE");
    gate->pins.push_back(Pin{"OUT", "1", PinElectricalType::Output, Point2D(0, 0), Point2D(-5, 0)});

    doc.addBlock("BUS_IC", {});
    BlockDefinition* busIc = doc.findBlock("BUS_IC");
    busIc->pins.push_back(Pin{"IO", "1", PinElectricalType::Bidirectional, Point2D(0, 0), Point2D(-5, 0)});

    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), gate, Point2D(0, 0)));
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), busIc, Point2D(0, 20)));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(0, 0), Point2D(0, 20)}));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);
    const bool hasContention = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.severity == ErcIssue::Severity::Warning && issue.message.find("contention") != std::string::npos;
    });
    REQUIRE(hasContention);
}

TEST_CASE("runErc does NOT warn about two OpenCollector pins wired together (real wired-OR pattern)",
         "[schematic][erc]") {
    Document doc;
    doc.addBlock("OC", {});
    BlockDefinition* oc = doc.findBlock("OC");
    oc->pins.push_back(Pin{"Y", "1", PinElectricalType::OpenCollector, Point2D(0, 0), Point2D(-5, 0)});

    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), oc, Point2D(0, 0)));
    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), oc, Point2D(0, 20)));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(0, 0), Point2D(0, 20)}));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);
    const bool hasWarning = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.message.find("OpenCollector") != std::string::npos;
    });
    REQUIRE_FALSE(hasWarning);
}

TEST_CASE("runErc flags two symbol instances sharing the same reference designator", "[schematic][erc]") {
    Document doc;
    const BlockDefinition* r = addTwoPinSymbol(doc, "R");

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 0));
    insertA->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 20));
    insertB->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertB));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);
    const bool hasDuplicate = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.severity == ErcIssue::Severity::Error && issue.message.find("Duplicate reference designator") != std::string::npos;
    });
    REQUIRE(hasDuplicate);
}

TEST_CASE("runErc warns about a component with a reference designator but no footprint assigned",
         "[schematic][erc]") {
    Document doc;
    const BlockDefinition* r = addTwoPinSymbol(doc, "R");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 0));
    insert->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insert));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);
    const bool hasMissingFootprint = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.message.find("no footprint assigned") != std::string::npos;
    });
    REQUIRE(hasMissingFootprint);
}

TEST_CASE("runErc does not warn about missing footprint once one is assigned", "[schematic][erc]") {
    Document doc;
    const BlockDefinition* r = addTwoPinSymbol(doc, "R");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 0));
    insert->setAttribute("REFDES", "R1");
    insert->setAttribute("FOOTPRINT", "R_0603");
    doc.addEntity(std::move(insert));

    const std::vector<Net> nets = computeNets(doc);
    const std::vector<ErcIssue> issues = runErc(doc, nets);
    const bool hasMissingFootprint = std::any_of(issues.begin(), issues.end(), [](const ErcIssue& issue) {
        return issue.message.find("no footprint assigned") != std::string::npos;
    });
    REQUIRE_FALSE(hasMissingFootprint);
}

TEST_CASE("formatNetlist lists nets with resolved reference designators", "[schematic][netlist]") {
    Document doc;
    const BlockDefinition* r = addTwoPinSymbol(doc, "R");

    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(0, 0));
    insertA->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), r, Point2D(100, 0));
    insertB->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(insertB));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(10, 0), Point2D(100, 0)}));

    const std::vector<Net> nets = computeNets(doc);
    const std::string text = formatNetlist(doc, nets);

    REQUIRE(text.find("R1.2") != std::string::npos);
    REQUIRE(text.find("R2.1") != std::string::npos);
}

TEST_CASE("registerBuiltinSymbols adds a usable, idempotent parts library", "[schematic][library]") {
    Document doc;
    registerBuiltinSymbols(doc);

    for (const char* name : {"R", "C", "D", "CONN2", "IC", "LED", "Q_NPN", "Q_PNP", "L", "SW", "CONN3", "CONN4"}) {
        const BlockDefinition* block = doc.findBlock(name);
        REQUIRE(block);
        REQUIRE(block->isSymbol());
    }
    REQUIRE(doc.findBlock("R")->pins.size() == 2);
    REQUIRE(doc.findBlock("IC")->pins.size() == 4);
    REQUIRE(doc.findBlock("Q_NPN")->pins.size() == 3);
    REQUIRE(doc.findBlock("Q_PNP")->pins.size() == 3);
    REQUIRE(doc.findBlock("CONN3")->pins.size() == 3);
    REQUIRE(doc.findBlock("CONN4")->pins.size() == 4);

    // Every schematic symbol must have a matching "<name>_FP" footprint
    // with the same pin/pad count, or a board using that part could never
    // be laid out at all -- previously true of C, D and CONN2.
    for (const char* name : {"R", "C", "D", "CONN2", "IC", "LED", "Q_NPN", "Q_PNP", "L", "SW", "CONN3", "CONN4"}) {
        const BlockDefinition* symbol = doc.findBlock(name);
        const BlockDefinition* footprint = doc.findBlock(std::string(name) + "_FP");
        REQUIRE(footprint);
        REQUIRE(footprint->isFootprint());
        REQUIRE(footprint->pads.size() == symbol->pins.size());
    }

    // Calling it again must not duplicate or reset anything a user already
    // customized on a builtin block.
    doc.findBlock("R")->pins[0].name = "customized";
    registerBuiltinSymbols(doc);
    REQUIRE(doc.findBlock("R")->pins[0].name == "customized");

    // A resistor's two pins, wired together, form a real net.
    auto insertA = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), doc.findBlock("R"),
                                                  Point2D(0, 0));
    const EntityId idA = insertA->id();
    doc.addEntity(std::move(insertA));
    auto insertB = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), doc.findBlock("C"),
                                                  Point2D(100, 0));
    const EntityId idB = insertB->id();
    doc.addEntity(std::move(insertB));
    doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(20, 0), Point2D(100, 0)}));

    const std::vector<Net> nets = computeNets(doc);
    const auto wiredNet = std::find_if(nets.begin(), nets.end(), [](const Net& n) { return n.pins.size() == 2; });
    REQUIRE(wiredNet != nets.end());
    const bool hasA = std::any_of(wiredNet->pins.begin(), wiredNet->pins.end(),
                                  [&](const NetPin& p) { return p.insertId == idA; });
    const bool hasB = std::any_of(wiredNet->pins.begin(), wiredNet->pins.end(),
                                  [&](const NetPin& p) { return p.insertId == idB; });
    REQUIRE(hasA);
    REQUIRE(hasB);
}
