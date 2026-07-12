add_library(lcad_core STATIC
    Color.h
    Ids.h
    geometry/Point2D.h
    geometry/BoundingBox.h
    geometry/Entity.h
    geometry/Line.h
    geometry/Line.cpp
    geometry/Circle.h
    geometry/Circle.cpp
    geometry/Arc.h
    geometry/Arc.cpp
    geometry/Polyline.h
    geometry/Polyline.cpp
    geometry/Ellipse.h
    geometry/Ellipse.cpp
    geometry/Spline.h
    geometry/Spline.cpp
    geometry/Text.h
    geometry/Text.cpp
    geometry/MText.h
    geometry/MText.cpp
    geometry/Dimension.h
    geometry/Dimension.cpp
    geometry/HatchPattern.h
    geometry/HatchPattern.cpp
    geometry/Hatch.h
    geometry/Hatch.cpp
    geometry/Leader.h
    geometry/Leader.cpp
    geometry/MLeader.h
    geometry/MLeader.cpp
    geometry/Table.h
    geometry/Table.cpp
    geometry/Insert.h
    geometry/Insert.cpp
    geometry/PolylineOps.h
    geometry/PolylineOps.cpp
    geometry/ModifyOps.h
    geometry/ModifyOps.cpp
    geometry/SnapGeometry.h
    geometry/SnapGeometry.cpp
    geometry/PointEnt.h
    geometry/ConstructionLine.h
    geometry/AttDef.h
    geometry/Intersect.h
    geometry/Intersect.cpp
    geometry/BoundaryTrace.h
    geometry/BoundaryTrace.cpp
    util/Expr.h
    util/Expr.cpp
    lisp/LispInterpreter.h
    lisp/LispInterpreter.cpp
    document/Layer.h
    document/Layout.h
    document/LineType.h
    document/LineType.cpp
    document/Block.h
    document/Command.h
    document/Command.cpp
    document/Commands.h
    document/Document.h
    document/Document.cpp
    io/DxfColors.h
    io/DxfColors.cpp
    io/DxfWriter.h
    io/DxfWriter.cpp
    io/DxfReader.h
    io/DxfReader.cpp
    io/DwgReader.h
    io/DwgReader.cpp
    io/DwgWriter.h
    io/DwgWriter.cpp
    io/Xref.h
    io/Xref.cpp
)

target_include_directories(lcad_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(lcad_core PUBLIC cxx_std_20)

# Optional DWG import via LibreDWG (https://www.gnu.org/software/libredwg/).
# Looked up in the usual prefixes plus ~/.local, where a from-source
# `./configure --prefix=$HOME/.local && make install` lands it.
option(LCAD_ENABLE_DWG "Enable DWG import via LibreDWG if available" ON)
if(LCAD_ENABLE_DWG)
    find_path(LIBREDWG_INCLUDE_DIR dwg_api.h HINTS $ENV{HOME}/.local/include)
    find_library(LIBREDWG_LIBRARY redwg HINTS $ENV{HOME}/.local/lib)
    if(LIBREDWG_INCLUDE_DIR AND LIBREDWG_LIBRARY)
        target_include_directories(lcad_core PRIVATE ${LIBREDWG_INCLUDE_DIR})
        target_link_libraries(lcad_core PUBLIC ${LIBREDWG_LIBRARY})
        target_compile_definitions(lcad_core PRIVATE LCAD_HAS_DWG)
        message(STATUS "DWG import enabled (LibreDWG: ${LIBREDWG_LIBRARY})")
    else()
        message(STATUS "LibreDWG not found; building without DWG import")
    endif()
endif()
