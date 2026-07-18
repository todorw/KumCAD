#include "core/pcb/GerberWriter.h"

#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace lcad {

namespace {

std::string formatCoord(double mm) {
    return std::to_string(static_cast<long long>(std::llround(mm * 1e5)));
}

enum class ApertureShape { Circle, Rect, Oval };

struct ApertureKey {
    ApertureShape shape;
    double dim1, dim2; // dim2 unused for Circle

    bool operator<(const ApertureKey& other) const {
        if (shape != other.shape) return shape < other.shape;
        if (std::abs(dim1 - other.dim1) > 1e-6) return dim1 < other.dim1;
        if (std::abs(dim2 - other.dim2) > 1e-6) return dim2 < other.dim2;
        return false;
    }
};

class ApertureTable {
public:
    int idFor(const ApertureKey& key) {
        auto it = m_ids.find(key);
        if (it != m_ids.end()) return it->second;
        const int id = m_next++;
        m_ids.emplace(key, id);
        m_order.push_back(key);
        return id;
    }

    std::string definitions() const {
        std::string out;
        int id = 10;
        for (const ApertureKey& key : m_order) {
            out += "%ADD" + std::to_string(id) + (key.shape == ApertureShape::Circle ? "C," : key.shape == ApertureShape::Rect ? "R," : "O,");
            if (key.shape == ApertureShape::Circle) {
                std::ostringstream oss;
                oss << key.dim1;
                out += oss.str();
            } else {
                std::ostringstream oss;
                oss << key.dim1 << "X" << key.dim2;
                out += oss.str();
            }
            out += "*%\n";
            ++id;
        }
        return out;
    }

private:
    std::map<ApertureKey, int> m_ids;
    std::vector<ApertureKey> m_order;
    int m_next = 10;
};

std::string isoCreationDate() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const std::tm utc = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// Infers a Gerber X2 %TF.FileFunction% from a KiCad-style layer name
// (F.Cu/B.Cu/F.SilkS/B.SilkS/F.Mask/B.Mask/Edge.Cuts/InN.Cu) -- the
// convention this codebase's own PCB layers already use (see
// CopperPourTests.cpp and friends). Falls back to a generic "Other,User"
// for a name that doesn't match any of these, a real, disclosed
// heuristic rather than a stored per-layer role field.
std::string inferFileFunction(const std::string& layerName) {
    if (layerName == "F.Cu") return "Copper,L1,Top";
    if (layerName == "B.Cu") return "Copper,L2,Bot";
    if (layerName == "F.SilkS") return "Legend,Top";
    if (layerName == "B.SilkS") return "Legend,Bot";
    if (layerName == "F.Mask") return "Soldermask,Top";
    if (layerName == "B.Mask") return "Soldermask,Bot";
    if (layerName == "F.Paste") return "Paste,Top";
    if (layerName == "B.Paste") return "Paste,Bot";
    if (layerName == "Edge.Cuts") return "Profile,NP";
    if (layerName.rfind("In", 0) == 0) {
        const auto dotCu = layerName.find(".Cu");
        if (dotCu != std::string::npos && dotCu > 2) return "Copper,L" + layerName.substr(2, dotCu - 2) + ",Inr";
    }
    return "Other,User";
}

} // namespace

bool writeGerberLayer(const Document& doc, LayerId layer, const std::string& path, std::string* errorOut) {
    std::vector<const TrackEntity*> tracks;
    std::vector<const ViaEntity*> vias;
    std::vector<const HatchEntity*> pours;
    std::vector<const InsertEntity*> footprints;
    for (const Entity* e : doc.entities()) {
        if (e->layer() != layer) continue;
        if (e->type() == EntityType::Track) tracks.push_back(static_cast<const TrackEntity*>(e));
        else if (e->type() == EntityType::Via) vias.push_back(static_cast<const ViaEntity*>(e));
        else if (e->type() == EntityType::Hatch) {
            const auto* hatch = static_cast<const HatchEntity*>(e);
            if (hatch->pattern() == HatchPattern::Solid) pours.push_back(hatch);
        } else if (e->type() == EntityType::Insert) {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (insert->block() && insert->block()->isFootprint()) footprints.push_back(insert);
        }
    }

    ApertureTable apertures;
    std::map<const TrackEntity*, int> trackAperture;
    for (const auto* t : tracks) trackAperture[t] = apertures.idFor({ApertureShape::Circle, t->width(), 0.0});
    std::map<const ViaEntity*, int> viaAperture;
    for (const auto* v : vias) viaAperture[v] = apertures.idFor({ApertureShape::Circle, v->diameter(), 0.0});
    std::map<const Pad*, int> padAperture;
    for (const auto* fp : footprints) {
        for (const Pad& pad : fp->block()->pads) {
            const ApertureShape shape =
                pad.shape == PadShape::Round ? ApertureShape::Circle : pad.shape == PadShape::Rect ? ApertureShape::Rect : ApertureShape::Oval;
            padAperture[&pad] = apertures.idFor({shape, pad.width, pad.height});
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }

    out << "G04 KumCAD Gerber export*\n";
    out << "%FSLAX35Y35*%\n";
    out << "%MOMM*%\n";
    // Real Gerber X2 file attributes -- a fab house's own auto-recognition
    // tooling reads %TF.FileFunction% to sort layers without relying on
    // filename conventions.
    out << "%TF.GenerationSoftware,KumCAD,KumCAD,1.0*%\n";
    out << "%TF.CreationDate," << isoCreationDate() << "*%\n";
    if (const Layer* layerInfo = doc.findLayer(layer)) {
        out << "%TF.FileFunction," << inferFileFunction(layerInfo->name) << "*%\n";
    }
    out << "%TF.FilePolarity,Positive*%\n";
    out << "%TF.Part,Single*%\n";
    out << "%LPD*%\n";
    out << apertures.definitions();

    for (const auto* t : tracks) {
        out << "D" << trackAperture[t] << "*\n";
        const auto& verts = t->vertices();
        for (std::size_t i = 0; i < verts.size(); ++i) {
            out << "X" << formatCoord(verts[i].x) << "Y" << formatCoord(verts[i].y) << (i == 0 ? "D02*\n" : "D01*\n");
        }
    }
    for (const auto* v : vias) {
        out << "D" << viaAperture[v] << "*\n";
        out << "X" << formatCoord(v->position().x) << "Y" << formatCoord(v->position().y) << "D03*\n";
    }
    for (const auto* fp : footprints) {
        const std::string* refdes = fp->attributeValue("REFDES");
        const bool haveRefdes = refdes && !refdes->empty();
        if (haveRefdes) out << "%TO.C," << *refdes << "*%\n";
        for (const auto& padWorld : fp->padWorldPositions()) {
            out << "D" << padAperture[padWorld.pad] << "*\n";
            out << "X" << formatCoord(padWorld.position.x) << "Y" << formatCoord(padWorld.position.y) << "D03*\n";
        }
        if (haveRefdes) out << "%TD*%\n";
    }
    for (const auto* pour : pours) {
        const auto& verts = pour->vertices();
        if (verts.size() < 3) continue;
        out << "G36*\n";
        for (std::size_t i = 0; i <= verts.size(); ++i) {
            const Point2D& p = verts[i % verts.size()];
            out << "X" << formatCoord(p.x) << "Y" << formatCoord(p.y) << (i == 0 ? "D02*\n" : "D01*\n");
        }
        out << "G37*\n";
    }

    out << "M02*\n";
    return true;
}

bool writeExcellonDrill(const Document& doc, const std::string& path, std::string* errorOut) {
    std::vector<std::pair<double, Point2D>> holes; // (diameter, position)
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Via) {
            const auto* via = static_cast<const ViaEntity*>(e);
            if (via->drillDiameter() > 1e-9) holes.emplace_back(via->drillDiameter(), via->position());
        } else if (e->type() == EntityType::Insert) {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (!insert->block() || !insert->block()->isFootprint()) continue;
            for (const auto& padWorld : insert->padWorldPositions()) {
                if (padWorld.pad->drillDiameter > 1e-9) holes.emplace_back(padWorld.pad->drillDiameter, padWorld.position);
            }
        }
    }

    std::map<double, int> toolNumber;
    std::vector<double> toolDiameters;
    for (const auto& [diameter, pos] : holes) {
        (void)pos;
        if (toolNumber.find(diameter) == toolNumber.end()) {
            toolNumber[diameter] = static_cast<int>(toolDiameters.size()) + 1;
            toolDiameters.push_back(diameter);
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }

    out << "M48\n";
    out << "METRIC,TZ\n";
    for (std::size_t i = 0; i < toolDiameters.size(); ++i) {
        std::ostringstream tool;
        tool << "T" << (i < 9 ? "0" : "") << (i + 1) << "C" << toolDiameters[i];
        out << tool.str() << "\n";
    }
    out << "%\n";

    for (std::size_t ti = 0; ti < toolDiameters.size(); ++ti) {
        std::ostringstream toolSel;
        toolSel << "T" << (ti < 9 ? "0" : "") << (ti + 1);
        out << toolSel.str() << "\n";
        for (const auto& [diameter, pos] : holes) {
            if (toolNumber[diameter] != static_cast<int>(ti) + 1) continue;
            out << "X" << static_cast<long long>(std::llround(pos.x * 1000)) << "Y"
                << static_cast<long long>(std::llround(pos.y * 1000)) << "\n";
        }
    }
    out << "M30\n";
    return true;
}

bool writePickAndPlace(const Document& doc, const std::string& path, std::string* errorOut) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }

    out << "RefDes,Footprint,X,Y,RotationDeg\n";
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (!insert->block() || !insert->block()->isFootprint()) continue;
        const std::string* refdes = insert->attributeValue("REFDES");
        out << (refdes && !refdes->empty() ? *refdes : "U" + std::to_string(insert->id())) << ","
            << insert->blockName() << "," << insert->position().x << "," << insert->position().y << ","
            << (insert->rotation() * 180.0 / M_PI) << "\n";
    }
    return true;
}

} // namespace lcad
