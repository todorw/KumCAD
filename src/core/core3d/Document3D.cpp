#include "core/core3d/Document3D.h"

#include "core/core3d/SketchToFace.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
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
        // gives each edge exactly once.
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(target, TopAbs_EDGE, edgeMap);
        BRepFilletAPI_MakeFillet filletBuilder(target);
        for (int i = 1; i <= edgeMap.Extent(); ++i) {
            filletBuilder.Add(f.p1, TopoDS::Edge(edgeMap(i)));
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
        for (int i = 1; i <= edgeMap.Extent(); ++i) {
            chamferBuilder.Add(f.p1, TopoDS::Edge(edgeMap(i)));
        }
        chamferBuilder.Build();
        ok = chamferBuilder.IsDone();
        if (ok) shape = chamferBuilder.Shape();
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

    m_shapes[static_cast<std::size_t>(index)] = shape;
    m_valid[static_cast<std::size_t>(index)] = ok && !shape.IsNull();
}

} // namespace lcad
