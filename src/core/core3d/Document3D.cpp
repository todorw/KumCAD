#include "core/core3d/Document3D.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

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
    }

    m_shapes[static_cast<std::size_t>(index)] = shape;
    m_valid[static_cast<std::size_t>(index)] = ok && !shape.IsNull();
}

} // namespace lcad
