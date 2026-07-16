#pragma once

#include <TopoDS_Shape.hxx>

#include <string>

namespace lcad {

// One 3D feature's result: a name (for the future feature-tree panel) and
// the OCCT shape it currently resolves to. This is deliberately just a
// flat shape holder for now -- the real parametric feature tree (params,
// dependency graph, recompute) is 3D Sprint 1's job, not foundations'.
struct Shape3D {
    std::string name;
    TopoDS_Shape shape;
};

} // namespace lcad
