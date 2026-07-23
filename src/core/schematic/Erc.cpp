#include "core/schematic/Erc.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/NoConnect.h"

#include <map>

namespace lcad {

namespace {

constexpr double kEpsilon = 1e-6;

const InsertEntity* findInsert(const Document& doc, EntityId id) {
    const Entity* e = doc.findEntity(id);
    if (!e || e->type() != EntityType::Insert) return nullptr;
    return static_cast<const InsertEntity*>(e);
}

const Pin* findPin(const InsertEntity& insert, const std::string& number, Point2D* worldOut) {
    for (const auto& pinWorld : insert.pinWorldPositions()) {
        if (pinWorld.pin->number == number) {
            if (worldOut) *worldOut = pinWorld.attach;
            return pinWorld.pin;
        }
    }
    return nullptr;
}

bool hasNoConnectAt(const Document& doc, const Point2D& world) {
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::NoConnect) continue;
        const auto* nc = static_cast<const NoConnectEntity*>(e);
        if (world.distanceTo(nc->position()) < kEpsilon) return true;
    }
    return false;
}

bool isHardDriver(PinElectricalType t) { return t == PinElectricalType::Output || t == PinElectricalType::PowerOutput; }

// Document-wide checks, independent of net topology: duplicate reference
// designators and missing footprint assignment. Both walk symbol Inserts
// directly rather than nets, the same REFDES/FOOTPRINT attribute
// convention Bom.cpp already relies on.
void runDocumentWideChecks(const Document& doc, std::vector<ErcIssue>& issues) {
    std::map<std::string, std::vector<EntityId>> byRefDes;
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (!insert->block() || !insert->block()->isSymbol()) continue;
        const std::string* refDes = insert->attributeValue("REFDES");
        if (!refDes || refDes->empty()) continue;
        byRefDes[*refDes].push_back(insert->id());

        const std::string* footprint = insert->attributeValue("FOOTPRINT");
        if (!footprint || footprint->empty()) {
            issues.push_back({ErcIssue::Severity::Warning, "Component " + *refDes + " has no footprint assigned",
                              insert->id()});
        }
    }
    for (const auto& [refDes, ids] : byRefDes) {
        if (ids.size() < 2) continue;
        issues.push_back({ErcIssue::Severity::Error,
                          "Duplicate reference designator \"" + refDes + "\" used by " + std::to_string(ids.size()) +
                              " components",
                          ids.front()});
    }
}

} // namespace

std::vector<ErcIssue> runErc(const Document& doc, const std::vector<Net>& nets) {
    std::vector<ErcIssue> issues;

    for (const Net& net : nets) {
        int outputCount = 0;
        int driverCount = 0; // Output or PowerOutput -- anything that can actually source this net
        int powerOutputCount = 0;
        int bidiOrTriStateCount = 0;
        int openCollectorCount = 0;
        bool hasPowerInput = false;
        EntityId firstPowerInputId = 0;
        for (const NetPin& np : net.pins) {
            const InsertEntity* insert = findInsert(doc, np.insertId);
            if (!insert) continue;
            Point2D world;
            const Pin* pin = findPin(*insert, np.pinNumber, &world);
            if (!pin) continue;

            if (net.pins.size() == 1) {
                if (pin->electricalType == PinElectricalType::NotConnected) continue;
                if (hasNoConnectAt(doc, world)) continue;
                issues.push_back({ErcIssue::Severity::Warning,
                                  "Pin " + pin->number + " (" + pin->name + ") is unconnected", np.insertId});
            } else if (pin->electricalType == PinElectricalType::NotConnected) {
                issues.push_back({ErcIssue::Severity::Warning,
                                  "Pin " + pin->number + " (" + pin->name +
                                      ") is marked Not Connected but is wired to net \"" + net.name + "\"",
                                  np.insertId});
            }

            if (pin->electricalType == PinElectricalType::Output) {
                ++outputCount;
            } else if (pin->electricalType == PinElectricalType::PowerOutput) {
                ++powerOutputCount;
            }
            if (isHardDriver(pin->electricalType)) {
                ++driverCount;
            } else if (pin->electricalType == PinElectricalType::Power && !hasPowerInput) {
                hasPowerInput = true;
                firstPowerInputId = np.insertId;
            } else if (pin->electricalType == PinElectricalType::Bidirectional ||
                       pin->electricalType == PinElectricalType::TriState) {
                ++bidiOrTriStateCount;
            } else if (pin->electricalType == PinElectricalType::OpenCollector) {
                ++openCollectorCount;
            }
        }
        if (outputCount > 1) {
            issues.push_back(
                {ErcIssue::Severity::Error, "Net \"" + net.name + "\" has " + std::to_string(outputCount) +
                                                " Output pins driving it (conflict)",
                 0});
        } else if (driverCount > 1) {
            // Covers the driver-conflict shapes the plain outputCount>1
            // check above doesn't: a single Output tied to one or more
            // PowerOutput pins, or two-or-more PowerOutput pins shorted
            // together (e.g. two regulator outputs tied to the same net).
            issues.push_back({ErcIssue::Severity::Error,
                              "Net \"" + net.name + "\" has " + std::to_string(driverCount) +
                                  " driving pins (Output/PowerOutput) tied together (conflict)",
                              0});
        }
        if ((outputCount + powerOutputCount) > 0 && bidiOrTriStateCount > 0) {
            issues.push_back({ErcIssue::Severity::Warning,
                              "Net \"" + net.name +
                                  "\" has a Bidirectional/TriState pin sharing a net with an Output/PowerOutput "
                                  "driver (potential bus contention)",
                              0});
        }
        if (outputCount > 0 && openCollectorCount > 0) {
            issues.push_back({ErcIssue::Severity::Warning,
                              "Net \"" + net.name +
                                  "\" has an OpenCollector pin sharing a net with an Output pin (verify pull-up "
                                  "wiring; OpenCollector-only nets are a normal wired-OR pattern)",
                              0});
        }
        // net.pins.size() > 1: a lone Power pin already gets the plain
        // "unconnected" warning above -- this check is specifically for
        // a Power pin that IS wired to something, just nothing that can
        // actually source it, so the two never double up on one pin.
        if (net.pins.size() > 1 && hasPowerInput && driverCount == 0) {
            issues.push_back({ErcIssue::Severity::Warning,
                              "Net \"" + net.name + "\" has a Power pin but no Output or PowerOutput pin "
                                                    "driving it (input power pin not driven)",
                              firstPowerInputId});
        }
    }

    runDocumentWideChecks(doc, issues);
    return issues;
}

} // namespace lcad
