#include "core/core3d/Piping.h"
#include "core/core3d/TechDraw.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lcad;
using Catch::Approx;

namespace {
double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}
} // namespace

TEST_CASE("buildPipeShape of a straight 2-point run is exactly one cylinder", "[core3d][piping]") {
    PipeRun run;
    run.outerRadius = 10.0;
    run.path = {{0, 0, 0}, {0, 0, 100.0}};

    const TopoDS_Shape shape = buildPipeShape(run);
    REQUIRE_FALSE(shape.IsNull());
    REQUIRE(volumeOf(shape) == Approx(M_PI * 10.0 * 10.0 * 100.0).margin(1e-3));
}

TEST_CASE("buildPipeShape of a bent run has more volume than one straight segment and less than the naive sum",
          "[core3d][piping]") {
    PipeRun run;
    run.outerRadius = 10.0;
    run.path = {{0, 0, 0}, {100.0, 0, 0}, {100.0, 100.0, 0}};

    const TopoDS_Shape shape = buildPipeShape(run);
    REQUIRE_FALSE(shape.IsNull());

    const double oneSegmentVolume = M_PI * 10.0 * 10.0 * 100.0;
    const double sphereVolume = (4.0 / 3.0) * M_PI * 10.0 * 10.0 * 10.0;
    const double naiveSum = 2 * oneSegmentVolume + sphereVolume; // if nothing overlapped (it does)

    const double actual = volumeOf(shape);
    REQUIRE(actual > oneSegmentVolume);   // real material was added by the second segment + joint
    REQUIRE(actual < naiveSum);           // fusing actually removed the overlap, not just concatenated volumes
}

TEST_CASE("buildPipeShape handles a multi-joint (3-bend) run", "[core3d][piping]") {
    PipeRun run;
    run.outerRadius = 8.0;
    run.path = {{0, 0, 0}, {200.0, 0, 0}, {200.0, 200.0, 0}, {200.0, 200.0, 150.0}, {0.0, 200.0, 150.0}};

    const TopoDS_Shape shape = buildPipeShape(run);
    REQUIRE_FALSE(shape.IsNull());
    REQUIRE(volumeOf(shape) > 0.0);
}

TEST_CASE("buildPipeShape rejects degenerate input", "[core3d][piping]") {
    PipeRun tooShort;
    tooShort.path = {{0, 0, 0}};
    REQUIRE(buildPipeShape(tooShort).IsNull());

    PipeRun zeroRadius;
    zeroRadius.outerRadius = 0.0;
    zeroRadius.path = {{0, 0, 0}, {100, 0, 0}};
    REQUIRE(buildPipeShape(zeroRadius).IsNull());
}

TEST_CASE("An isometric TechDraw projection of a pipe run produces edges", "[core3d][piping]") {
    PipeRun run;
    run.outerRadius = 10.0;
    run.path = {{0, 0, 0}, {100.0, 0, 0}, {100.0, 100.0, 0}};
    const TopoDS_Shape shape = buildPipeShape(run);
    REQUIRE_FALSE(shape.IsNull());

    const TechDrawView view = projectView(shape, ViewDirection::Iso);
    REQUIRE_FALSE(view.edges.empty());
}
