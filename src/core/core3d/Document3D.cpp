#include "core/core3d/Document3D.h"

#include "core/core3d/SketchToFace.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace lcad {

int Document3D::addFeature(Feature3D feature) {
    const int index = static_cast<int>(m_features.size());
    m_features.push_back(std::move(feature));
    m_shapes.emplace_back();
    m_valid.push_back(false);
    recomputeOne(index);
    return index;
}

void Document3D::updateFeature(int index, Feature3D feature) {
    if (index < 0 || index >= static_cast<int>(m_features.size())) return;
    m_features[static_cast<std::size_t>(index)] = std::move(feature);

    // Recompute index, then walk forward: anything referencing an index
    // we just recomputed is a dependent and needs recomputing too. Since
    // booleans only ever reference earlier indices, one forward pass in
    // list order is enough to propagate the change transitively.
    std::vector<bool> dirty(m_features.size(), false);
    dirty[static_cast<std::size_t>(index)] = true;
    recomputeOne(index);

    for (std::size_t i = static_cast<std::size_t>(index) + 1; i < m_features.size(); ++i) {
        const Feature3D& f = m_features[i];
        if (f.isBoolean() && ((f.inputA >= 0 && dirty[static_cast<std::size_t>(f.inputA)]) ||
                              (f.inputB >= 0 && dirty[static_cast<std::size_t>(f.inputB)]))) {
            recomputeOne(static_cast<int>(i));
            dirty[i] = true;
        }
    }
}

bool Document3D::removeFeature(int index) {
    if (index < 0 || index >= static_cast<int>(m_features.size())) return false;
    for (const Feature3D& f : m_features) {
        if (f.inputA == index || f.inputB == index) return false;
    }
    m_features.erase(m_features.begin() + index);
    m_shapes.erase(m_shapes.begin() + index);
    m_valid.erase(m_valid.begin() + index);
    // Every input index greater than the removed one shifts down by one.
    for (Feature3D& f : m_features) {
        if (f.inputA > index) --f.inputA;
        if (f.inputB > index) --f.inputB;
    }
    return true;
}

void Document3D::insertFeatureAt(int index, Feature3D feature) {
    if (index < 0 || index > static_cast<int>(m_features.size())) return;
    for (Feature3D& f : m_features) {
        if (f.inputA >= index) ++f.inputA;
        if (f.inputB >= index) ++f.inputB;
    }
    m_features.insert(m_features.begin() + index, std::move(feature));
    m_shapes.insert(m_shapes.begin() + index, TopoDS_Shape());
    m_valid.insert(m_valid.begin() + index, false);
    for (std::size_t i = static_cast<std::size_t>(index); i < m_features.size(); ++i) recomputeOne(static_cast<int>(i));
}

int Document3D::addSketch(Sketch sketch) {
    m_sketches.push_back(std::move(sketch));
    return static_cast<int>(m_sketches.size()) - 1;
}

int Document3D::addImportedShape(TopoDS_Shape shape) {
    m_importedShapes.push_back(std::move(shape));
    return static_cast<int>(m_importedShapes.size()) - 1;
}

const Feature3D* Document3D::findFeature(int index) const {
    if (index < 0 || index >= static_cast<int>(m_features.size())) return nullptr;
    return &m_features[static_cast<std::size_t>(index)];
}

const TopoDS_Shape& Document3D::shapeAt(int index) const {
    static const TopoDS_Shape kEmpty;
    if (index < 0 || index >= static_cast<int>(m_shapes.size())) return kEmpty;
    return m_shapes[static_cast<std::size_t>(index)];
}

bool Document3D::isValid(int index) const {
    if (index < 0 || index >= static_cast<int>(m_valid.size())) return false;
    return m_valid[static_cast<std::size_t>(index)];
}

void Document3D::recomputeOne(int index) {
    Feature3D& f = m_features[static_cast<std::size_t>(index)];
    const gp_Ax2 axes(gp_Pnt(f.posX, f.posY, f.posZ), gp_Dir(0, 0, 1));

    TopoDS_Shape shape;
    bool ok = true;

    switch (f.type) {
    case FeatureType::Box:
        if (f.p1 > 1e-9 && f.p2 > 1e-9 && f.p3 > 1e-9) shape = BRepPrimAPI_MakeBox(axes, f.p1, f.p2, f.p3).Shape();
        else ok = false;
        break;
    case FeatureType::Cylinder:
        if (f.p1 > 1e-9 && f.p2 > 1e-9) shape = BRepPrimAPI_MakeCylinder(axes, f.p1, f.p2).Shape();
        else ok = false;
        break;
    case FeatureType::Sphere:
        if (f.p1 > 1e-9) shape = BRepPrimAPI_MakeSphere(axes, f.p1).Shape();
        else ok = false;
        break;
    case FeatureType::Cone:
        if (f.p3 > 1e-9 && (f.p1 > 1e-9 || f.p2 > 1e-9)) shape = BRepPrimAPI_MakeCone(axes, f.p1, f.p2, f.p3).Shape();
        else ok = false;
        break;
    case FeatureType::Torus:
        if (f.p1 > 1e-9 && f.p2 > 1e-9) shape = BRepPrimAPI_MakeTorus(axes, f.p1, f.p2).Shape();
        else ok = false;
        break;
    case FeatureType::Wedge:
        if (f.p1 > 1e-9 && f.p2 > 1e-9 && f.p3 > 1e-9 && f.p4 >= 0.0) {
            shape = BRepPrimAPI_MakeWedge(axes, f.p1, f.p2, f.p3, f.p4).Shape();
        } else {
            ok = false;
        }
        break;
    case FeatureType::Union:
    case FeatureType::Cut:
    case FeatureType::Intersect: {
        if (f.inputA < 0 || f.inputA >= index || f.inputB < 0 || f.inputB >= index || !isValid(f.inputA) ||
            !isValid(f.inputB)) {
            ok = false;
            break;
        }
        const TopoDS_Shape& a = m_shapes[static_cast<std::size_t>(f.inputA)];
        const TopoDS_Shape& b = m_shapes[static_cast<std::size_t>(f.inputB)];
        if (f.type == FeatureType::Union) {
            BRepAlgoAPI_Fuse op(a, b);
            ok = op.IsDone();
            if (ok) shape = op.Shape();
        } else if (f.type == FeatureType::Cut) {
            BRepAlgoAPI_Cut op(a, b);
            ok = op.IsDone();
            if (ok) shape = op.Shape();
        } else {
            BRepAlgoAPI_Common op(a, b);
            ok = op.IsDone();
            if (ok) shape = op.Shape();
        }
        break;
    }
    case FeatureType::Pad:
    case FeatureType::Revolve: {
        const double dirMag = std::sqrt(f.dirX * f.dirX + f.dirY * f.dirY + f.dirZ * f.dirZ);
        if (f.sketchIndex < 0 || f.sketchIndex >= static_cast<int>(m_sketches.size()) || dirMag < 1e-9) {
            ok = false;
            break;
        }
        const auto face = sketchToFace(m_sketches[static_cast<std::size_t>(f.sketchIndex)]);
        if (!face) {
            ok = false;
            break;
        }

        TopoDS_Shape extruded;
        if (f.type == FeatureType::Pad) {
            if (f.p1 <= 1e-9) {
                ok = false;
                break;
            }
            // The sketch itself always lies at Z=0 in its own local XY
            // plane (see SketchToFace.cpp); posX/Y/Z is where in 3D space
            // that plane -- where the extrusion actually starts from --
            // sits. Revolve doesn't need this: its posX/Y/Z is already the
            // rotation axis's point, a different, already-meaningful use.
            TopoDS_Shape faceShape = *face;
            if (std::abs(f.posX) > 1e-12 || std::abs(f.posY) > 1e-12 || std::abs(f.posZ) > 1e-12) {
                gp_Trsf move;
                move.SetTranslation(gp_Vec(f.posX, f.posY, f.posZ));
                faceShape = BRepBuilderAPI_Transform(faceShape, move, true).Shape();
            }
            gp_Vec vec(f.dirX / dirMag * f.p1, f.dirY / dirMag * f.p1, f.dirZ / dirMag * f.p1);
            extruded = BRepPrimAPI_MakePrism(faceShape, vec).Shape();
        } else {
            if (std::abs(f.p1) <= 1e-9) {
                ok = false;
                break;
            }
            const gp_Ax1 axis(gp_Pnt(f.posX, f.posY, f.posZ), gp_Dir(f.dirX, f.dirY, f.dirZ));
            extruded = BRepPrimAPI_MakeRevol(*face, axis, f.p1 * M_PI / 180.0).Shape();
        }

        if (f.inputA >= 0) {
            if (f.inputA >= index || !isValid(f.inputA)) {
                ok = false;
                break;
            }
            const TopoDS_Shape& target = m_shapes[static_cast<std::size_t>(f.inputA)];
            if (f.cutMode) {
                BRepAlgoAPI_Cut op(target, extruded);
                ok = op.IsDone();
                if (ok) shape = op.Shape();
            } else {
                BRepAlgoAPI_Fuse op(target, extruded);
                ok = op.IsDone();
                if (ok) shape = op.Shape();
            }
        } else {
            shape = extruded;
        }
        break;
    }
    case FeatureType::Fillet: {
        if (f.inputA < 0 || f.inputA >= index || !isValid(f.inputA) || f.p1 <= 1e-9) {
            ok = false;
            break;
        }
        const TopoDS_Shape& target = m_shapes[static_cast<std::size_t>(f.inputA)];
        // A raw TopExp_Explorer visits an edge once per adjacent face, so
        // a box's 12 edges (each shared by 2 faces) would be added twice
        // each -- BRepFilletAPI_MakeFillet rejects that. TopExp::MapShapes
        // gives each edge exactly once, in the same 1-based ordering
        // Pick3D.h's pickEdge numbers edges by (0-based there; +1 here).
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(target, TopAbs_EDGE, edgeMap);
        BRepFilletAPI_MakeFillet filletBuilder(target);
        int addedCount = 0;
        if (f.edgeIndices.empty()) {
            for (int i = 1; i <= edgeMap.Extent(); ++i) {
                filletBuilder.Add(f.p1, TopoDS::Edge(edgeMap(i)));
                ++addedCount;
            }
        } else {
            for (int edgeIndex : f.edgeIndices) {
                if (edgeIndex < 0 || edgeIndex >= edgeMap.Extent()) continue;
                filletBuilder.Add(f.p1, TopoDS::Edge(edgeMap(edgeIndex + 1)));
                ++addedCount;
            }
        }
        // BRepFilletAPI_MakeFillet::Build() throws if asked to build with
        // zero edges added (e.g. every requested edgeIndices entry was out
        // of range) -- a real edge case caught by a test expecting a
        // graceful no-op, not by inspection. Treated as an invalid
        // feature, the same way any other out-of-range reference is.
        if (addedCount == 0) {
            ok = false;
            break;
        }
        filletBuilder.Build(); // unlike the primitive builders, this one doesn't build in its constructor
        ok = filletBuilder.IsDone();
        if (ok) shape = filletBuilder.Shape();
        break;
    }
    case FeatureType::Chamfer: {
        if (f.inputA < 0 || f.inputA >= index || !isValid(f.inputA) || f.p1 <= 1e-9) {
            ok = false;
            break;
        }
        const TopoDS_Shape& target = m_shapes[static_cast<std::size_t>(f.inputA)];
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(target, TopAbs_EDGE, edgeMap);
        BRepFilletAPI_MakeChamfer chamferBuilder(target);
        int addedCount = 0;
        if (f.edgeIndices.empty()) {
            for (int i = 1; i <= edgeMap.Extent(); ++i) {
                chamferBuilder.Add(f.p1, TopoDS::Edge(edgeMap(i)));
                ++addedCount;
            }
        } else {
            for (int edgeIndex : f.edgeIndices) {
                if (edgeIndex < 0 || edgeIndex >= edgeMap.Extent()) continue;
                chamferBuilder.Add(f.p1, TopoDS::Edge(edgeMap(edgeIndex + 1)));
                ++addedCount;
            }
        }
        if (addedCount == 0) {
            ok = false;
            break;
        }
        chamferBuilder.Build();
        ok = chamferBuilder.IsDone();
        if (ok) shape = chamferBuilder.Shape();
        break;
    }
    case FeatureType::Shell: {
        if (f.inputA < 0 || f.inputA >= index || !isValid(f.inputA) || f.p1 <= 1e-9) {
            ok = false;
            break;
        }
        const TopoDS_Shape& target = m_shapes[static_cast<std::size_t>(f.inputA)];
        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(target, TopAbs_FACE, faceMap);
        TopTools_ListOfShape facesToRemove;
        for (int faceIndex : f.faceIndices) {
            if (faceIndex < 0 || faceIndex >= faceMap.Extent()) continue;
            facesToRemove.Append(faceMap(faceIndex + 1));
        }
        // A Shell needs at least one open face -- a fully sealed hollow
        // shell isn't buildable/useful here (unlike Fillet/Chamfer, whose
        // empty edgeIndices means "every edge" instead).
        if (facesToRemove.IsEmpty()) {
            ok = false;
            break;
        }
        BRepOffsetAPI_MakeThickSolid shellBuilder;
        // Negated: a positive p1 (the wall thickness the user actually
        // types) means "hollow inward," matching the sign convention a
        // real Shell/Thickness tool's UI uses -- OCCT's own offset
        // direction convention is the opposite of that.
        shellBuilder.MakeThickSolidByJoin(target, facesToRemove, -f.p1, 1e-3);
        ok = shellBuilder.IsDone();
        if (ok) shape = shellBuilder.Shape();
        break;
    }
    case FeatureType::Loft: {
        // A Sketch always lies at its own local Z=0 (see Pad's own
        // comment on this) -- Loft's p1 is the total height, each
        // profile evenly spaced along it in listed order (profile 0 at
        // Z=0, the last at Z=p1), the same "reuse p1 contextually"
        // convention this codebase already uses for Pad/Shell/etc.
        if (f.sketchIndices.size() < 2 || f.p1 <= 1e-9) {
            ok = false;
            break;
        }
        BRepOffsetAPI_ThruSections loftBuilder(true); // true = build a solid, not just a shell surface
        bool everyProfileValid = true;
        const double stepZ = f.p1 / static_cast<double>(f.sketchIndices.size() - 1);
        for (std::size_t i = 0; i < f.sketchIndices.size(); ++i) {
            const int sketchIdx = f.sketchIndices[i];
            if (sketchIdx < 0 || sketchIdx >= static_cast<int>(m_sketches.size())) {
                everyProfileValid = false;
                break;
            }
            const auto face = sketchToFace(m_sketches[static_cast<std::size_t>(sketchIdx)]);
            if (!face) {
                everyProfileValid = false;
                break;
            }
            TopoDS_Shape faceShape = *face;
            const double z = static_cast<double>(i) * stepZ;
            if (std::abs(z) > 1e-12) {
                gp_Trsf move;
                move.SetTranslation(gp_Vec(0.0, 0.0, z));
                faceShape = BRepBuilderAPI_Transform(faceShape, move, true).Shape();
            }
            loftBuilder.AddWire(BRepTools::OuterWire(TopoDS::Face(faceShape)));
        }
        if (!everyProfileValid) {
            ok = false;
            break;
        }
        loftBuilder.Build();
        ok = loftBuilder.IsDone();
        if (ok) shape = loftBuilder.Shape();
        break;
    }
    case FeatureType::Sweep: {
        if (f.sketchIndex < 0 || f.sketchIndex >= static_cast<int>(m_sketches.size()) || f.pathSketchIndex < 0 ||
            f.pathSketchIndex >= static_cast<int>(m_sketches.size())) {
            ok = false;
            break;
        }
        const auto profileFace = sketchToFace(m_sketches[static_cast<std::size_t>(f.sketchIndex)]);
        const Sketch& pathSketch = m_sketches[static_cast<std::size_t>(f.pathSketchIndex)];
        // Exactly one straight line -- see FeatureType::Sweep's own
        // comment on why a multi-segment/curved path isn't supported.
        if (!profileFace || pathSketch.lines().size() != 1) {
            ok = false;
            break;
        }

        const SketchLine& firstLine = pathSketch.lines()[0];
        const Point2D& pStart = pathSketch.points()[static_cast<std::size_t>(firstLine.p1)];
        const Point2D& pEnd = pathSketch.points()[static_cast<std::size_t>(firstLine.p2)];
        const Point2D dir2D = pEnd - pStart;
        const double dirLen = dir2D.length();
        if (dirLen < 1e-9) {
            ok = false;
            break;
        }

        // The profile sketch lies flat in its own local XY plane (normal
        // +Z), same as every other sketch-consuming feature -- but a
        // pipe sweep needs the profile perpendicular to the spine's own
        // initial direction (its plane's normal parallel to that
        // direction, not the +Z axis), or MakePipe just extrudes it
        // edge-on instead of sweeping a real cross-section. Rotating the
        // profile's own +Z axis onto the spine's start direction (a
        // general vector-to-vector rotation: axis = their cross
        // product, angle = the angle between them) fixes that, then
        // translating it to the spine's own start point positions it
        // where the sweep actually begins.
        const gp_Dir fromDir(0.0, 0.0, 1.0);
        const gp_Dir toDir(dir2D.x / dirLen, dir2D.y / dirLen, 0.0);
        TopoDS_Shape profileShape = *profileFace;
        gp_Trsf rotate;
        if (!fromDir.IsParallel(toDir, 1e-9)) {
            rotate.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), fromDir.Crossed(toDir)), fromDir.Angle(toDir));
        }
        gp_Trsf translate;
        translate.SetTranslation(gp_Vec(pStart.x, pStart.y, 0.0));
        profileShape = BRepBuilderAPI_Transform(profileShape, translate.Multiplied(rotate), true).Shape();

        BRepBuilderAPI_MakeWire wireBuilder;
        for (const SketchLine& line : pathSketch.lines()) {
            const Point2D& a = pathSketch.points()[static_cast<std::size_t>(line.p1)];
            const Point2D& b = pathSketch.points()[static_cast<std::size_t>(line.p2)];
            wireBuilder.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(a.x, a.y, 0.0), gp_Pnt(b.x, b.y, 0.0)));
        }
        if (!wireBuilder.IsDone()) {
            ok = false;
            break;
        }

        BRepOffsetAPI_MakePipe pipeBuilder(wireBuilder.Wire(), profileShape);
        pipeBuilder.Build();
        ok = pipeBuilder.IsDone();
        if (ok) shape = pipeBuilder.Shape();
        break;
    }
    case FeatureType::Draft: {
        const double dirMag = std::sqrt(f.dirX * f.dirX + f.dirY * f.dirY + f.dirZ * f.dirZ);
        if (f.inputA < 0 || f.inputA >= index || !isValid(f.inputA) || f.faceIndices.empty() || dirMag < 1e-9) {
            ok = false;
            break;
        }
        const TopoDS_Shape& target = m_shapes[static_cast<std::size_t>(f.inputA)];
        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(target, TopAbs_FACE, faceMap);

        const gp_Dir pullDir(f.dirX / dirMag, f.dirY / dirMag, f.dirZ / dirMag);
        // The neutral plane's own normal is the pull direction itself --
        // the common "draft angle measured from the pull direction"
        // convention, and the only one expressible without a second
        // direction field this codebase's Feature3D doesn't have.
        const gp_Pln neutralPlane(gp_Pnt(f.posX, f.posY, f.posZ), pullDir);
        const double angleRad = f.p1 * M_PI / 180.0;

        BRepOffsetAPI_DraftAngle draftBuilder(target);
        int addedCount = 0;
        bool anyAddFailed = false;
        for (int faceIndex : f.faceIndices) {
            if (faceIndex < 0 || faceIndex >= faceMap.Extent()) continue;
            draftBuilder.Add(TopoDS::Face(faceMap(faceIndex + 1)), pullDir, angleRad, neutralPlane);
            if (!draftBuilder.AddDone()) {
                anyAddFailed = true;
                break;
            }
            ++addedCount;
        }
        // Same "zero faces actually added" caution Fillet/Chamfer/Shell
        // already apply -- Build() below isn't safe to call blind here
        // either.
        if (anyAddFailed || addedCount == 0) {
            ok = false;
            break;
        }
        draftBuilder.Build();
        ok = draftBuilder.IsDone();
        if (ok) shape = draftBuilder.Shape();
        break;
    }
    case FeatureType::LinearPattern:
    case FeatureType::PolarPattern: {
        const double dirMag = std::sqrt(f.dirX * f.dirX + f.dirY * f.dirY + f.dirZ * f.dirZ);
        if (f.inputA < 0 || f.inputA >= index || !isValid(f.inputA) || f.count < 1 || dirMag < 1e-9) {
            ok = false;
            break;
        }
        const TopoDS_Shape& source = m_shapes[static_cast<std::size_t>(f.inputA)];
        shape = source;
        for (int i = 1; i < f.count; ++i) {
            gp_Trsf trsf;
            if (f.type == FeatureType::LinearPattern) {
                const gp_Vec step(f.dirX / dirMag * f.p1 * i, f.dirY / dirMag * f.p1 * i, f.dirZ / dirMag * f.p1 * i);
                trsf.SetTranslation(step);
            } else {
                const gp_Ax1 axis(gp_Pnt(f.posX, f.posY, f.posZ), gp_Dir(f.dirX, f.dirY, f.dirZ));
                const double angleStep = (f.p1 * M_PI / 180.0) / static_cast<double>(f.count - 1 > 0 ? f.count - 1 : 1);
                trsf.SetRotation(axis, angleStep * i);
            }
            const TopoDS_Shape copy = BRepBuilderAPI_Transform(source, trsf, true).Shape();
            BRepAlgoAPI_Fuse op(shape, copy);
            if (!op.IsDone()) {
                ok = false;
                break;
            }
            shape = op.Shape();
        }
        break;
    }
    case FeatureType::Imported: {
        if (f.importIndex < 0 || f.importIndex >= static_cast<int>(m_importedShapes.size())) {
            ok = false;
            break;
        }
        shape = m_importedShapes[static_cast<std::size_t>(f.importIndex)];
        if (std::abs(f.posX) > 1e-12 || std::abs(f.posY) > 1e-12 || std::abs(f.posZ) > 1e-12) {
            gp_Trsf move;
            move.SetTranslation(gp_Vec(f.posX, f.posY, f.posZ));
            shape = BRepBuilderAPI_Transform(shape, move, true).Shape();
        }
        break;
    }
    case FeatureType::Mirror: {
        const double dirMag = std::sqrt(f.dirX * f.dirX + f.dirY * f.dirY + f.dirZ * f.dirZ);
        if (f.inputA < 0 || f.inputA >= index || !isValid(f.inputA) || dirMag < 1e-9) {
            ok = false;
            break;
        }
        const TopoDS_Shape& source = m_shapes[static_cast<std::size_t>(f.inputA)];
        gp_Trsf trsf;
        const gp_Ax2 mirrorPlane(gp_Pnt(f.posX, f.posY, f.posZ), gp_Dir(f.dirX, f.dirY, f.dirZ));
        trsf.SetMirror(mirrorPlane);
        const TopoDS_Shape mirrored = BRepBuilderAPI_Transform(source, trsf, true).Shape();
        BRepAlgoAPI_Fuse op(source, mirrored);
        ok = op.IsDone();
        if (ok) shape = op.Shape();
        break;
    }
    }

    // An additional placement rotation -- primitives and Imported only
    // (see Feature3D.h's own field comment for why other types ignore
    // this), applied last so it spins the already-positioned shape in
    // place around (posX,posY,posZ) rather than needing to be composed
    // with each type's own construction transform.
    const bool rotatable = f.type == FeatureType::Box || f.type == FeatureType::Cylinder ||
                           f.type == FeatureType::Sphere || f.type == FeatureType::Cone ||
                           f.type == FeatureType::Torus || f.type == FeatureType::Wedge ||
                           f.type == FeatureType::Imported;
    if (ok && !shape.IsNull() && rotatable && std::abs(f.rotAngle) > 1e-12) {
        const double axisLen = std::sqrt(f.rotAxisX * f.rotAxisX + f.rotAxisY * f.rotAxisY + f.rotAxisZ * f.rotAxisZ);
        if (axisLen > 1e-9) {
            gp_Trsf rotate;
            rotate.SetRotation(gp_Ax1(gp_Pnt(f.posX, f.posY, f.posZ), gp_Dir(f.rotAxisX, f.rotAxisY, f.rotAxisZ)),
                               f.rotAngle * M_PI / 180.0);
            shape = BRepBuilderAPI_Transform(shape, rotate, true).Shape();
        }
    }

    m_shapes[static_cast<std::size_t>(index)] = shape;
    m_valid[static_cast<std::size_t>(index)] = ok && !shape.IsNull();
}

} // namespace lcad
