#pragma once

#include "core/core3d/Shape3D.h"

#include <vector>

namespace lcad {

// The 3D-modeling document: an ordered list of named shapes. A flat list
// on purpose -- the parametric feature tree (dependency graph, recompute
// on parameter edit) is 3D Sprint 1's job; foundations just needs
// somewhere for a shape to live between being built and being displayed.
class Document3D {
public:
    void addShape(std::string name, TopoDS_Shape shape);
    const std::vector<Shape3D>& shapes() const { return m_shapes; }
    void clear() { m_shapes.clear(); }

private:
    std::vector<Shape3D> m_shapes;
};

} // namespace lcad
