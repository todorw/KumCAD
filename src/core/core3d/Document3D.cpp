#include "core/core3d/Document3D.h"

namespace lcad {

void Document3D::addShape(std::string name, TopoDS_Shape shape) {
    m_shapes.push_back({std::move(name), std::move(shape)});
}

} // namespace lcad
