#include "core/pcb/SpecctraWriter.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/pcb/Ratsnest.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>

namespace lcad {

namespace {

// DSN's conventional unit is micrometers; KumCAD's drawing units are
// assumed millimeters (matching DrcRules' own "typically mm" assumption).
constexpr double kUmPerUnit = 1000.0;

std::string sanitizeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return out.empty() ? "UNNAMED" : out;
}

std::string padstackName(const Pad& pad) {
    std::ostringstream out;
    out << (pad.shape == PadShape::Round ? "Round" : "Rect") << "_" << pad.width << "x" << pad.height;
    return sanitizeName(out.str());
}

} // namespace

bool writeSpecctraDsn(const Document& doc, const std::vector<ImportedNet>& nets, const std::string& path,
                      std::string* errorOut) {
    std::vector<const InsertEntity*> footprints;
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (insert->block() && insert->block()->isFootprint()) footprints.push_back(insert);
    }
    if (footprints.empty()) {
        if (errorOut) *errorOut = "No placed footprints to export";
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }

    // Board boundary: the bounding box of every placed pad, padded by 5mm
    // -- this codebase has no separate board-outline concept yet (see
    // SpecctraWriter.h's own disclosure).
    double minX = std::numeric_limits<double>::max(), minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest(), maxY = std::numeric_limits<double>::lowest();
    for (const auto* insert : footprints) {
        for (const auto& padWorld : insert->padWorldPositions()) {
            minX = std::min(minX, padWorld.position.x);
            minY = std::min(minY, padWorld.position.y);
            maxX = std::max(maxX, padWorld.position.x);
            maxY = std::max(maxY, padWorld.position.y);
        }
    }
    constexpr double kMargin = 5.0;
    minX -= kMargin;
    minY -= kMargin;
    maxX += kMargin;
    maxY += kMargin;

    out << "(pcb \"kumcad_board\"\n";
    out << "  (parser\n";
    out << "    (string_quote \")\n";
    out << "    (space_in_quoted_tokens on)\n";
    out << "    (host_cad \"KumCAD\")\n";
    out << "    (host_version \"1.0\")\n";
    out << "  )\n";
    out << "  (resolution um 10)\n";
    out << "  (unit um)\n";
    out << "  (structure\n";
    out << "    (layer F.Cu (type signal))\n";
    out << "    (layer B.Cu (type signal))\n";
    out << "    (boundary\n";
    out << "      (rect pcb " << (minX * kUmPerUnit) << " " << (minY * kUmPerUnit) << " " << (maxX * kUmPerUnit) << " "
        << (maxY * kUmPerUnit) << ")\n";
    out << "    )\n";
    out << "  )\n";

    // One padstack per distinct pad shape/size, one footprint image per
    // distinct block definition -- both deduplicated and reused across
    // every placement, not repeated per instance.
    std::map<std::string, const Pad*> padstacks;
    std::map<std::string, const BlockDefinition*> images;
    for (const auto* insert : footprints) {
        images.emplace(insert->block()->name, insert->block());
        for (const Pad& pad : insert->block()->pads) padstacks.emplace(padstackName(pad), &pad);
    }

    out << "  (placement\n";
    std::map<std::string, std::vector<const InsertEntity*>> byBlockName;
    for (const auto* insert : footprints) byBlockName[insert->block()->name].push_back(insert);
    for (const auto& [blockName, instances] : byBlockName) {
        out << "    (component \"" << sanitizeName(blockName) << "\"\n";
        for (const auto* insert : instances) {
            const std::string* refdes = insert->attributeValue("REFDES");
            const std::string designator = refdes && !refdes->empty() ? *refdes : ("U" + std::to_string(insert->id()));
            const double rotationDeg = insert->rotation() * 180.0 / M_PI;
            out << "      (place " << sanitizeName(designator) << " " << (insert->position().x * kUmPerUnit) << " "
                << (insert->position().y * kUmPerUnit) << " front " << rotationDeg << ")\n";
        }
        out << "    )\n";
    }
    out << "  )\n";

    out << "  (library\n";
    for (const auto& [name, pad] : padstacks) {
        out << "    (padstack " << name << "\n";
        if (pad->shape == PadShape::Round) {
            out << "      (shape (circle F.Cu " << (pad->width * kUmPerUnit) << "))\n";
            out << "      (shape (circle B.Cu " << (pad->width * kUmPerUnit) << "))\n";
        } else {
            out << "      (shape (rect F.Cu " << -(pad->width / 2 * kUmPerUnit) << " " << -(pad->height / 2 * kUmPerUnit)
                << " " << (pad->width / 2 * kUmPerUnit) << " " << (pad->height / 2 * kUmPerUnit) << "))\n";
            out << "      (shape (rect B.Cu " << -(pad->width / 2 * kUmPerUnit) << " " << -(pad->height / 2 * kUmPerUnit)
                << " " << (pad->width / 2 * kUmPerUnit) << " " << (pad->height / 2 * kUmPerUnit) << "))\n";
        }
        out << "    )\n";
    }
    for (const auto& [blockName, block] : images) {
        out << "    (image " << sanitizeName(blockName) << "\n";
        for (const Pad& pad : block->pads) {
            out << "      (pin " << padstackName(pad) << " " << sanitizeName(pad.number) << " "
                << (pad.position.x * kUmPerUnit) << " " << (pad.position.y * kUmPerUnit) << ")\n";
        }
        out << "    )\n";
    }
    out << "  )\n";

    out << "  (network\n";
    for (const ImportedNet& net : nets) {
        out << "    (net \"" << net.name << "\"\n";
        out << "      (pins";
        for (const ImportedNetPin& pin : net.pins) out << " " << sanitizeName(pin.refDes) << "-" << sanitizeName(pin.pinNumber);
        out << ")\n";
        out << "    )\n";
    }
    out << "  )\n";
    out << ")\n";

    return static_cast<bool>(out);
}

} // namespace lcad
