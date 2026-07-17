#include "core/core3d/Document3D.h"
#include "core/core3d/LispBindings3D.h"
#include "core/lisp/LispInterpreter.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

using namespace lcad;
using Catch::Approx;

namespace {

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

struct Fixture {
    Document3D doc;
    LispInterpreter interp{[](const std::string&) {}};
    Fixture() { registerLisp3DBindings(interp, doc); }
    LispInterpreter::RunResult run(const std::string& src) { return interp.run(src); }
};

} // namespace

TEST_CASE("BOX3D/CYLINDER3D/SPHERE3D create real features and return their index", "[core3d][lisp3d]") {
    Fixture fx;

    const auto boxResult = fx.run("(box3d 10 5 4)");
    REQUIRE(boxResult.ok);
    REQUIRE(boxResult.resultText == "0");
    REQUIRE(fx.doc.isValid(0));
    REQUIRE(volumeOf(fx.doc.shapeAt(0)) == Approx(10.0 * 5.0 * 4.0).margin(1e-6));

    const auto cylResult = fx.run("(cylinder3d 3 10)");
    REQUIRE(cylResult.ok);
    REQUIRE(fx.doc.isValid(1));
    REQUIRE(volumeOf(fx.doc.shapeAt(1)) == Approx(M_PI * 9.0 * 10.0).margin(1e-3));

    const auto sphereResult = fx.run("(sphere3d 2)");
    REQUIRE(sphereResult.ok);
    REQUIRE(fx.doc.isValid(2));
    REQUIRE(volumeOf(fx.doc.shapeAt(2)) == Approx(4.0 / 3.0 * M_PI * 8.0).margin(1e-3));
}

TEST_CASE("BOX3D honors optional position arguments", "[core3d][lisp3d]") {
    Fixture fx;
    fx.run("(box3d 10 10 10 5 6 7)");
    REQUIRE(fx.doc.isValid(0));
    REQUIRE(fx.doc.findFeature(0)->posX == Approx(5.0));
    REQUIRE(fx.doc.findFeature(0)->posY == Approx(6.0));
    REQUIRE(fx.doc.findFeature(0)->posZ == Approx(7.0));
}

TEST_CASE("UNION3D/CUT3D/INTERSECT3D combine feature indices from Lisp", "[core3d][lisp3d]") {
    Fixture fx;
    fx.run("(setq a (box3d 20 20 20)) (setq b (cylinder3d 3 20 10 10 0))");
    const auto cutResult = fx.run("(cut3d a b)");
    REQUIRE(cutResult.ok);
    const int cutIdx = static_cast<int>(std::stod(cutResult.resultText));
    REQUIRE(fx.doc.isValid(cutIdx));

    const double boxVolume = 20.0 * 20.0 * 20.0;
    const double cylVolume = M_PI * 9.0 * 20.0;
    REQUIRE(volumeOf(fx.doc.shapeAt(cutIdx)) == Approx(boxVolume - cylVolume).margin(1.0));
}

TEST_CASE("PAD3D extrudes an existing sketch by index", "[core3d][lisp3d]") {
    Fixture fx;
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0), true);
    const int p1 = sketch.addPoint(Point2D(8, 0));
    const int p2 = sketch.addPoint(Point2D(8, 6));
    const int p3 = sketch.addPoint(Point2D(0, 6));
    sketch.addLine(p0, p1);
    sketch.addLine(p1, p2);
    sketch.addLine(p2, p3);
    sketch.addLine(p3, p0);
    const int sketchIdx = fx.doc.addSketch(sketch);

    const auto result = fx.run("(pad3d " + std::to_string(sketchIdx) + " 5)");
    REQUIRE(result.ok);
    const int padIdx = static_cast<int>(std::stod(result.resultText));
    REQUIRE(fx.doc.isValid(padIdx));
    REQUIRE(volumeOf(fx.doc.shapeAt(padIdx)) == Approx(8.0 * 6.0 * 5.0).margin(1e-6));
}

TEST_CASE("VOLUME3D and BBOX3D query a feature's real geometry", "[core3d][lisp3d]") {
    Fixture fx;
    fx.run("(box3d 10 4 6)");

    const auto volResult = fx.run("(volume3d 0)");
    REQUIRE(volResult.ok);
    REQUIRE(std::stod(volResult.resultText) == Approx(10.0 * 4.0 * 6.0).margin(1e-6));

    const auto bboxResult = fx.run("(bbox3d 0)");
    REQUIRE(bboxResult.ok);
    // (xmin ymin zmin xmax ymax zmax) for a box built at the origin with
    // gp_Ax2's default axes -- checked numerically (not as an exact
    // string) since Bnd_Box pads its own bounds by a tiny built-in gap.
    std::string body = bboxResult.resultText.substr(1, bboxResult.resultText.size() - 2); // strip ( )
    std::istringstream numbers(body);
    std::vector<double> bounds;
    double value = 0.0;
    while (numbers >> value) bounds.push_back(value);
    REQUIRE(bounds.size() == 6);
    const std::vector<double> expected = {0.0, 0.0, 0.0, 10.0, 4.0, 6.0};
    for (std::size_t i = 0; i < 6; ++i) REQUIRE(bounds[i] == Approx(expected[i]).margin(1e-4));
}

TEST_CASE("Creation/query functions return nil for an invalid index or degenerate parameters", "[core3d][lisp3d]") {
    Fixture fx;
    REQUIRE(fx.run("(box3d 0 5 5)").resultText == "nil"); // degenerate: dx=0
    REQUIRE(fx.run("(volume3d 999)").resultText == "nil");
    REQUIRE(fx.run("(bbox3d 999)").resultText == "nil");
}

TEST_CASE("EXPORTSTEP3D writes a real STEP file", "[core3d][lisp3d]") {
    Fixture fx;
    fx.run("(box3d 10 10 10)");

    const auto path = std::filesystem::temp_directory_path() /
                      ("kumcad_lisp3d_test_" + std::to_string(std::rand()) + ".step");
    const auto result = fx.run("(exportstep3d \"" + path.string() + "\")");
    REQUIRE(result.ok);
    REQUIRE(result.resultText == "T");
    REQUIRE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST_CASE("Unknown 3D function names still fail like any other unbound function", "[core3d][lisp3d]") {
    Fixture fx;
    const auto result = fx.run("(nosuchfunction3d 1 2 3)");
    REQUIRE_FALSE(result.ok);
}

TEST_CASE("3D bindings compose with ordinary Lisp control flow", "[core3d][lisp3d]") {
    Fixture fx;
    // A little script: build 3 boxes of increasing size, keep the total
    // volume as it goes -- proves the bindings interoperate with setq/
    // while/user variables, not just work as one-off calls.
    const auto result = fx.run(
        "(setq i 1 total 0) "
        "(while (<= i 3) "
        "  (setq idx (box3d i i i)) "
        "  (setq total (+ total (volume3d idx))) "
        "  (setq i (1+ i))) "
        "total");
    REQUIRE(result.ok);
    REQUIRE(std::stod(result.resultText) == Approx(1.0 + 8.0 + 27.0));
}
