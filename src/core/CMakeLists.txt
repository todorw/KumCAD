add_library(lcad_core STATIC
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
    geometry/Text.h
    geometry/Text.cpp
    geometry/Dimension.h
    geometry/Dimension.cpp
    geometry/Intersect.h
    geometry/Intersect.cpp
    document/Layer.h
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
)

target_include_directories(lcad_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(lcad_core PUBLIC cxx_std_20)
