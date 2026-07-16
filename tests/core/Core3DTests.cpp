#include "core/core3d/Document3D.h"

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("OpenCASCADE builds a real B-rep box with the correct volume", "[core3d][occt]") {
    // The actual proof this integration works: not just "it links", but a
    // real kernel operation (BRepPrimAPI_MakeBox) producing a shape whose
    // measured volume (via BRepGProp, OCCT's own mass-properties module)
    // matches width*height*depth. No display/viewport is needed for this,
    // unlike the 3D viewport itself, which can't be verified in this
    // environment (no display, no working GL rasterizer under offscreen --
    // see the DrawingView precedent for the same limitation on the 2D side).
    BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
    const TopoDS_Shape box = makeBox.Shape();
    REQUIRE_FALSE(box.IsNull());

    GProp_GProps props;
    BRepGProp::VolumeProperties(box, props);
    REQUIRE(props.Mass() == Approx(10.0 * 20.0 * 30.0).margin(1e-6));
}

TEST_CASE("Document3D holds named shapes", "[core3d]") {
    lcad::Document3D doc;
    REQUIRE(doc.shapes().empty());

    BRepPrimAPI_MakeBox makeBox(5.0, 5.0, 5.0);
    doc.addShape("Box1", makeBox.Shape());
    REQUIRE(doc.shapes().size() == 1);
    REQUIRE(doc.shapes()[0].name == "Box1");
    REQUIRE_FALSE(doc.shapes()[0].shape.IsNull());

    doc.clear();
    REQUIRE(doc.shapes().empty());
}
