#include "core/core3d/Document3D.h"
#include "core/core3d/StepIges.h"

#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pnt.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace lcad;
using Catch::Approx;

namespace {

// RAII temp file path, same convention as DxfIoTests.cpp's TempDxfPath.
struct TempPath {
    std::filesystem::path path;
    explicit TempPath(const std::string& ext)
        : path(std::filesystem::temp_directory_path() / ("kumcad_step_iges_test_" + std::to_string(std::rand()) + ext)) {}
    ~TempPath() { std::filesystem::remove(path); }
};

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

double boundingDiagonal(const TopoDS_Shape& shape) {
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Document3D makeBoxDocument(double dx, double dy, double dz) {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = dx;
    box.p2 = dy;
    box.p3 = dz;
    doc.addFeature(box);
    return doc;
}

} // namespace

TEST_CASE("writeStep exports a document's tip feature and readStep round-trips its exact volume", "[core3d][step]") {
    TempPath temp(".step");
    Document3D doc = makeBoxDocument(10.0, 5.0, 4.0);

    REQUIRE(writeStep(doc, temp.path.string()));

    const TopoDS_Shape shape = readStep(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    REQUIRE(volumeOf(shape) == Approx(10.0 * 5.0 * 4.0).margin(1e-3));
}

TEST_CASE("A shape read back from STEP can be used as an Imported feature", "[core3d][step]") {
    TempPath temp(".step");
    Document3D doc = makeBoxDocument(6.0, 6.0, 6.0);
    REQUIRE(writeStep(doc, temp.path.string()));
    const TopoDS_Shape shape = readStep(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());

    Document3D reloaded;
    const int importIdx = reloaded.addImportedShape(shape);
    Feature3D imported;
    imported.type = FeatureType::Imported;
    imported.importIndex = importIdx;
    const int featureIdx = reloaded.addFeature(imported);

    REQUIRE(reloaded.isValid(featureIdx));
    REQUIRE(volumeOf(reloaded.shapeAt(featureIdx)) == Approx(6.0 * 6.0 * 6.0).margin(1e-3));
}

TEST_CASE("writeStep skips features consumed as another feature's input and only exports tips", "[core3d][step]") {
    TempPath temp(".step");
    Document3D doc;
    Feature3D boxA;
    boxA.type = FeatureType::Box;
    boxA.p1 = boxA.p2 = boxA.p3 = 10.0;
    const int a = doc.addFeature(boxA);

    Feature3D boxB;
    boxB.type = FeatureType::Box;
    boxB.p1 = boxB.p2 = boxB.p3 = 4.0;
    const int b = doc.addFeature(boxB);

    Feature3D cut;
    cut.type = FeatureType::Cut;
    cut.inputA = a;
    cut.inputB = b;
    doc.addFeature(cut);

    // boxA and boxB are both consumed by the cut; only the cut result
    // should be exported, not the two raw boxes.
    REQUIRE(writeStep(doc, temp.path.string()));
    const TopoDS_Shape shape = readStep(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    REQUIRE(volumeOf(shape) == Approx(1000.0 - 64.0).margin(1e-3));
}

TEST_CASE("writeIges exports a shape and readIges round-trips its bounding box", "[core3d][iges]") {
    TempPath temp(".igs");
    Document3D doc = makeBoxDocument(8.0, 3.0, 2.0);

    REQUIRE(writeIges(doc, temp.path.string()));

    const TopoDS_Shape shape = readIges(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    // IGES is an older, primarily surface-oriented format -- a solid can
    // come back as a shell rather than a classified solid, so this checks
    // the round-tripped bounding box rather than volume (a real, disclosed
    // limitation of IGES specifically, not of this writer/reader).
    const double expectedDiagonal = std::sqrt(8.0 * 8.0 + 3.0 * 3.0 + 2.0 * 2.0);
    REQUIRE(boundingDiagonal(shape) == Approx(expectedDiagonal).margin(1e-3));
}

TEST_CASE("writeStep/writeIges fail cleanly on a document with no valid tip shape", "[core3d][step][iges]") {
    TempPath stepTemp(".step");
    TempPath igesTemp(".igs");
    Document3D empty;
    REQUIRE_FALSE(writeStep(empty, stepTemp.path.string()));
    REQUIRE_FALSE(writeIges(empty, igesTemp.path.string()));
}

TEST_CASE("writeStep(vector<TopoDS_Shape>) exports several independent shapes into one file",
         "[core3d][step]") {
    TempPath temp(".step");
    const TopoDS_Shape boxA = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TopoDS_Shape boxB = BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape();

    REQUIRE(writeStep(std::vector<TopoDS_Shape>{boxA, boxB}, temp.path.string()));

    const TopoDS_Shape shape = readStep(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    REQUIRE(volumeOf(shape) == Approx(10.0 * 5.0 * 4.0 + 2.0 * 2.0 * 2.0).margin(1e-3));
}

TEST_CASE("writeStep(vector<TopoDS_Shape>) skips null shapes and fails cleanly when nothing is left",
         "[core3d][step]") {
    TempPath temp(".step");
    REQUIRE_FALSE(writeStep(std::vector<TopoDS_Shape>{}, temp.path.string()));
    REQUIRE_FALSE(writeStep(std::vector<TopoDS_Shape>{TopoDS_Shape()}, temp.path.string()));

    // A real shape alongside a null one still exports -- the null is
    // skipped, not treated as a hard failure.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(3.0, 3.0, 3.0).Shape();
    REQUIRE(writeStep(std::vector<TopoDS_Shape>{TopoDS_Shape(), box}, temp.path.string()));
    const TopoDS_Shape shape = readStep(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    REQUIRE(volumeOf(shape) == Approx(27.0).margin(1e-3));
}

TEST_CASE("writeIges(vector<TopoDS_Shape>) exports several shapes and round-trips their combined bounding box",
         "[core3d][iges]") {
    TempPath temp(".igs");
    const TopoDS_Shape boxA = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 4.0, 4.0, 4.0).Shape();
    const TopoDS_Shape boxB = BRepPrimAPI_MakeBox(gp_Pnt(10, 0, 0), 4.0, 4.0, 4.0).Shape();

    REQUIRE(writeIges(std::vector<TopoDS_Shape>{boxA, boxB}, temp.path.string()));

    const TopoDS_Shape shape = readIges(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    // Combined bounding box spans x=[0,14], y=[0,4], z=[0,4] -- boxB sits
    // 10 units away from boxA along X, so this is only possible if BOTH
    // shapes actually made it into the file, not just one.
    const double expectedDiagonal = std::sqrt(14.0 * 14.0 + 4.0 * 4.0 + 4.0 * 4.0);
    REQUIRE(boundingDiagonal(shape) == Approx(expectedDiagonal).margin(1e-3));
}

TEST_CASE("writeStl exports a document's tip feature and readStl round-trips its exact volume for a flat-faced box",
         "[core3d][stl]") {
    TempPath temp(".stl");
    Document3D doc = makeBoxDocument(10.0, 5.0, 4.0);

    REQUIRE(writeStl(doc, temp.path.string()));

    const TopoDS_Shape shape = readStl(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    // A box's faces are already flat, so tessellating them (STL is
    // triangles-only, unlike STEP/IGES's exact curves/surfaces) loses
    // nothing -- the volume comes back exact, unlike a curved shape where
    // the tessellation's own deflection tolerance would matter.
    REQUIRE(volumeOf(shape) == Approx(10.0 * 5.0 * 4.0).margin(1e-3));
}

TEST_CASE("writeStl/readStl round-trip a curved shape within its own tessellation tolerance", "[core3d][stl]") {
    TempPath temp(".stl");
    Document3D doc;
    Feature3D cyl;
    cyl.type = FeatureType::Cylinder;
    cyl.p1 = 5.0;  // radius
    cyl.p2 = 10.0; // height
    doc.addFeature(cyl);

    REQUIRE(writeStl(doc, temp.path.string(), 0.01));

    const TopoDS_Shape shape = readStl(temp.path.string());
    REQUIRE_FALSE(shape.IsNull());
    const double exactVolume = M_PI * 5.0 * 5.0 * 10.0;
    // A tessellated cylinder's polygon approximation always UNDER-estimates
    // the true volume (chords cut inside the true circular arc) -- a fine
    // deflection keeps that error small, generously bounded here.
    REQUIRE(volumeOf(shape) == Approx(exactVolume).epsilon(0.02));
}

TEST_CASE("writeStl(vector<TopoDS_Shape>) combines several shapes into one file", "[core3d][stl]") {
    TempPath temp(".stl");
    const TopoDS_Shape boxA = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 10.0, 10.0, 10.0).Shape();
    const TopoDS_Shape boxB = BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 5.0, 5.0, 5.0).Shape();

    REQUIRE(writeStl(std::vector<TopoDS_Shape>{boxA, boxB}, temp.path.string()));

    const TopoDS_Shape combined = readStl(temp.path.string());
    REQUIRE_FALSE(combined.IsNull());
    REQUIRE(volumeOf(combined) == Approx(1000.0 + 125.0).margin(1e-2));
}

TEST_CASE("writeStl fails cleanly on an empty/null shape list; readStl fails cleanly on a missing file",
         "[core3d][stl]") {
    TempPath temp(".stl");
    REQUIRE_FALSE(writeStl(std::vector<TopoDS_Shape>{}, temp.path.string()));
    REQUIRE_FALSE(writeStl(std::vector<TopoDS_Shape>{TopoDS_Shape()}, temp.path.string()));

    const TopoDS_Shape shape = readStl("/nonexistent/path/does_not_exist.stl");
    REQUIRE(shape.IsNull());
}
