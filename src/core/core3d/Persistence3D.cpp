#include "core/core3d/Persistence3D.h"

#include "core/io/Zip.h"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <cstdio>
#include <map>
#include <sstream>

namespace lcad {

namespace {

// istringstream's >> reads whitespace-delimited tokens, which can't
// represent an empty string or embedded spaces/newlines directly -- percent
// -encode the handful of characters that would break that, same idea as
// URL encoding, just scoped to this one file format.
const std::string kEmptyMarker = "@@EMPTY@@";

std::string encodeToken(const std::string& s) {
    if (s.empty()) return kEmptyMarker;
    std::string out;
    for (unsigned char c : s) {
        if (c == ' ' || c == '%' || c == '\n' || c == '\r' || c == '\t') {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string decodeToken(const std::string& s) {
    if (s == kEmptyMarker) return "";
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 3;
        } else {
            out += s[i];
            ++i;
        }
    }
    return out;
}

std::string serializeDocument(const Document3D& doc) {
    std::ostringstream out;
    out << "KCAD3D 5\n";

    // Named document variables (see Document3D::setVariable) arrived in
    // format version 5, alongside per-feature expressions below.
    out << "VARIABLES " << doc.variables().size() << "\n";
    for (const auto& [name, value] : doc.variables()) {
        out << encodeToken(name) << " " << value << "\n";
    }

    const auto& sketches = doc.sketches();
    out << "SKETCHES " << sketches.size() << "\n";
    for (const Sketch& sketch : sketches) {
        out << "SKETCH\n";
        out << "POINTS " << sketch.points().size() << "\n";
        for (std::size_t i = 0; i < sketch.points().size(); ++i) {
            const Point2D& p = sketch.points()[i];
            out << p.x << " " << p.y << " " << (sketch.pointFixed()[i] ? 1 : 0) << "\n";
        }
        out << "LINES " << sketch.lines().size() << "\n";
        for (const SketchLine& l : sketch.lines()) {
            out << l.p1 << " " << l.p2 << " " << (l.construction ? 1 : 0) << "\n";
        }
        out << "CIRCLES " << sketch.circles().size() << "\n";
        for (const SketchCircle& c : sketch.circles()) {
            out << c.center << " " << c.radius << " " << (c.construction ? 1 : 0) << "\n";
        }
        out << "CONSTRAINTS " << sketch.constraints().size() << "\n";
        for (const SketchConstraint& k : sketch.constraints()) {
            out << static_cast<int>(k.type) << " " << k.geomA << " " << k.geomB << " " << k.pointA << " "
                << k.pointB << " " << k.value << "\n";
        }
        out << "ENDSKETCH\n";
    }

    out << "FEATURES " << doc.features().size() << "\n";
    for (const Feature3D& f : doc.features()) {
        out << static_cast<int>(f.type) << " " << encodeToken(f.name) << "\n";
        out << f.p1 << " " << f.p2 << " " << f.p3 << " " << f.p4 << "\n";
        out << f.posX << " " << f.posY << " " << f.posZ << "\n";
        out << f.dirX << " " << f.dirY << " " << f.dirZ << "\n";
        out << f.rotAxisX << " " << f.rotAxisY << " " << f.rotAxisZ << " " << f.rotAngle << "\n";
        out << f.inputA << " " << f.inputB << " " << (f.cutMode ? 1 : 0) << "\n";
        out << f.sketchIndex << " " << f.count << " " << f.importIndex << " " << f.pathSketchIndex << "\n";
        out << "EDGEINDICES " << f.edgeIndices.size();
        for (int edgeIndex : f.edgeIndices) out << " " << edgeIndex;
        out << "\n";
        out << "FACEINDICES " << f.faceIndices.size();
        for (int faceIndex : f.faceIndices) out << " " << faceIndex;
        out << "\n";
        out << "SKETCHINDICES " << f.sketchIndices.size();
        for (int sketchIdx : f.sketchIndices) out << " " << sketchIdx;
        out << "\n";
        out << "EXPRESSIONS " << f.expressions.size();
        for (const auto& [fieldName, expr] : f.expressions) {
            out << " " << encodeToken(fieldName) << " " << encodeToken(expr);
        }
        out << "\n";
    }

    out << "IMPORTEDSHAPES " << doc.importedShapes().size() << "\n";
    return out.str();
}

struct ParsedDocument3D {
    std::vector<Sketch> sketches;
    std::vector<Feature3D> features;
    std::size_t importedShapeCount = 0;
    std::vector<std::pair<std::string, double>> variables;
};

bool expectTag(std::istringstream& in, const char* tag) {
    std::string got;
    in >> got;
    return static_cast<bool>(in) && got == tag;
}

bool parseDocumentText(const std::string& text, ParsedDocument3D& parsed) {
    std::istringstream in(text);
    if (!expectTag(in, "KCAD3D")) return false;
    int version = 0;
    in >> version;
    if (!in) return false;

    // Named variables arrived in format version 5; an older file has none.
    if (version >= 5) {
        if (!expectTag(in, "VARIABLES")) return false;
        std::size_t variableCount = 0;
        in >> variableCount;
        for (std::size_t v = 0; v < variableCount; ++v) {
            std::string encodedName;
            double value = 0.0;
            in >> encodedName >> value;
            if (!in) return false;
            parsed.variables.emplace_back(decodeToken(encodedName), value);
        }
    }

    std::size_t sketchCount = 0;
    if (!expectTag(in, "SKETCHES")) return false;
    in >> sketchCount;
    if (!in) return false;

    for (std::size_t s = 0; s < sketchCount; ++s) {
        if (!expectTag(in, "SKETCH")) return false;
        Sketch sketch;

        std::size_t n = 0;
        if (!expectTag(in, "POINTS")) return false;
        in >> n;
        for (std::size_t i = 0; i < n; ++i) {
            double x = 0, y = 0;
            int fixed = 0;
            in >> x >> y >> fixed;
            sketch.addPoint(Point2D(x, y), fixed != 0);
        }

        if (!expectTag(in, "LINES")) return false;
        in >> n;
        for (std::size_t i = 0; i < n; ++i) {
            int p1 = -1, p2 = -1, construction = 0;
            in >> p1 >> p2 >> construction;
            sketch.addLine(p1, p2, construction != 0);
        }

        if (!expectTag(in, "CIRCLES")) return false;
        in >> n;
        for (std::size_t i = 0; i < n; ++i) {
            int center = -1, construction = 0;
            double radius = 0;
            in >> center >> radius >> construction;
            sketch.addCircle(center, radius, construction != 0);
        }

        if (!expectTag(in, "CONSTRAINTS")) return false;
        in >> n;
        for (std::size_t i = 0; i < n; ++i) {
            int type = 0, geomA = -1, geomB = -1, pointA = -1, pointB = -1;
            double value = 0;
            in >> type >> geomA >> geomB >> pointA >> pointB >> value;
            SketchConstraint k;
            k.type = static_cast<SketchConstraintType>(type);
            k.geomA = geomA;
            k.geomB = geomB;
            k.pointA = pointA;
            k.pointB = pointB;
            k.value = value;
            sketch.addConstraint(k);
        }

        if (!expectTag(in, "ENDSKETCH")) return false;
        if (!in) return false;
        parsed.sketches.push_back(std::move(sketch));
    }

    std::size_t featureCount = 0;
    if (!expectTag(in, "FEATURES")) return false;
    in >> featureCount;
    if (!in) return false;

    for (std::size_t i = 0; i < featureCount; ++i) {
        int type = 0;
        std::string encodedName;
        in >> type >> encodedName;
        Feature3D f;
        f.type = static_cast<FeatureType>(type);
        f.name = decodeToken(encodedName);
        in >> f.p1 >> f.p2 >> f.p3 >> f.p4;
        in >> f.posX >> f.posY >> f.posZ;
        in >> f.dirX >> f.dirY >> f.dirZ;
        // Rotation + per-edge/per-face selection arrived in format version
        // 2 -- a version-1 file (written before either existed) simply
        // doesn't have these fields, so Feature3D's own defaults (no
        // rotation, "every edge"/no shell faces) apply, matching exactly
        // what that older file actually meant.
        if (version >= 2) in >> f.rotAxisX >> f.rotAxisY >> f.rotAxisZ >> f.rotAngle;
        int cutMode = 0;
        in >> f.inputA >> f.inputB >> cutMode;
        f.cutMode = cutMode != 0;
        in >> f.sketchIndex >> f.count >> f.importIndex;
        // Sweep's own path-sketch reference arrived in format version 4
        // -- an older file has no Sweep features to lose, since the type
        // didn't exist for it to have used yet.
        if (version >= 4) in >> f.pathSketchIndex;
        if (version >= 2) {
            if (!expectTag(in, "EDGEINDICES")) return false;
            std::size_t edgeIndexCount = 0;
            in >> edgeIndexCount;
            for (std::size_t e = 0; e < edgeIndexCount; ++e) {
                int edgeIndex = -1;
                in >> edgeIndex;
                f.edgeIndices.push_back(edgeIndex);
            }
            if (!expectTag(in, "FACEINDICES")) return false;
            std::size_t faceIndexCount = 0;
            in >> faceIndexCount;
            for (std::size_t fc = 0; fc < faceIndexCount; ++fc) {
                int faceIndex = -1;
                in >> faceIndex;
                f.faceIndices.push_back(faceIndex);
            }
        }
        // Loft's multi-sketch profile list arrived in format version 3;
        // a version-2 (or older) file has no Loft features to lose,
        // since the feature type didn't exist yet either.
        if (version >= 3) {
            if (!expectTag(in, "SKETCHINDICES")) return false;
            std::size_t sketchIndexCount = 0;
            in >> sketchIndexCount;
            for (std::size_t s = 0; s < sketchIndexCount; ++s) {
                int sketchIdx = -1;
                in >> sketchIdx;
                f.sketchIndices.push_back(sketchIdx);
            }
        }
        // Expression-driven parameters arrived in format version 5; an
        // older file's features simply have no expressions bound.
        if (version >= 5) {
            if (!expectTag(in, "EXPRESSIONS")) return false;
            std::size_t expressionCount = 0;
            in >> expressionCount;
            for (std::size_t e = 0; e < expressionCount; ++e) {
                std::string encodedField, encodedExpr;
                in >> encodedField >> encodedExpr;
                f.expressions[decodeToken(encodedField)] = decodeToken(encodedExpr);
            }
        }
        if (!in) return false;
        parsed.features.push_back(f);
    }

    if (!expectTag(in, "IMPORTEDSHAPES")) return false;
    in >> parsed.importedShapeCount;
    return static_cast<bool>(in);
}

} // namespace

bool saveDocument3D(const Document3D& doc, const std::string& path) {
    std::vector<std::pair<std::string, std::string>> entries;
    entries.emplace_back("document.txt", serializeDocument(doc));

    const auto& imported = doc.importedShapes();
    for (std::size_t i = 0; i < imported.size(); ++i) {
        std::ostringstream shapeOut;
        BRepTools::Write(imported[i], shapeOut);
        entries.emplace_back("imported_" + std::to_string(i) + ".brep", shapeOut.str());
    }

    return writeZip(path, entries);
}

bool loadDocument3D(Document3D& doc, const std::string& path) {
    std::vector<std::pair<std::string, std::string>> entries;
    if (!readZip(path, entries)) return false;

    std::map<std::string, std::string> byName;
    for (auto& [name, content] : entries) byName.emplace(std::move(name), std::move(content));

    const auto docIt = byName.find("document.txt");
    if (docIt == byName.end()) return false;

    ParsedDocument3D parsed;
    if (!parseDocumentText(docIt->second, parsed)) return false;

    for (std::size_t i = 0; i < parsed.importedShapeCount; ++i) {
        const auto blobIt = byName.find("imported_" + std::to_string(i) + ".brep");
        if (blobIt == byName.end()) return false;
        std::istringstream shapeIn(blobIt->second);
        TopoDS_Shape shape;
        BRep_Builder builder;
        BRepTools::Read(shape, shapeIn, builder);
        doc.addImportedShape(shape);
    }

    // Variables must be in place BEFORE features are added below, since
    // addFeature recomputes immediately and any expression referencing a
    // variable needs it to already exist.
    for (const auto& [name, value] : parsed.variables) doc.setVariable(name, value);
    for (Sketch& sketch : parsed.sketches) doc.addSketch(std::move(sketch));
    for (Feature3D& feature : parsed.features) doc.addFeature(feature);
    return true;
}

} // namespace lcad
