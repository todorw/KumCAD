#include "core/core3d/Pick3D.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("pickFace hits the top face of a box straight down its normal", "[core3d][pick]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    PickRay ray;
    ray.origin = {5.0, 5.0, 100.0};
    ray.direction = {0.0, 0.0, -1.0};

    const auto hit = pickFace(box, ray);
    REQUIRE(hit.has_value());
    REQUIRE(hit->point[0] == Approx(5.0).margin(1e-6));
    REQUIRE(hit->point[1] == Approx(5.0).margin(1e-6));
    REQUIRE(hit->point[2] == Approx(10.0).margin(1e-6)); // the top face, not the bottom
    REQUIRE(hit->distance == Approx(90.0).margin(1e-6));
    REQUIRE(hit->normal[2] == Approx(1.0).margin(1e-6)); // outward normal points +Z
}

TEST_CASE("pickFace returns the NEAREST face when the ray passes through the whole box", "[core3d][pick]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    PickRay ray;
    ray.origin = {5.0, 5.0, -100.0}; // below the box, looking up through it
    ray.direction = {0.0, 0.0, 1.0};

    const auto hit = pickFace(box, ray);
    REQUIRE(hit.has_value());
    // The bottom face (z=0) is nearer to the ray origin than the top (z=10).
    REQUIRE(hit->point[2] == Approx(0.0).margin(1e-6));
    REQUIRE(hit->normal[2] == Approx(-1.0).margin(1e-6)); // outward normal points -Z on the bottom face
}

TEST_CASE("pickFace returns nullopt for a ray that misses the shape entirely", "[core3d][pick]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    PickRay ray;
    ray.origin = {100.0, 100.0, 100.0};
    ray.direction = {0.0, 0.0, -1.0}; // parallel to the box, well off to the side

    REQUIRE_FALSE(pickFace(box, ray).has_value());
}

TEST_CASE("pickFace on a cylinder hits the curved side face with a radial normal", "[core3d][pick]") {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(5.0, 20.0).Shape();
    PickRay ray;
    ray.origin = {100.0, 0.0, 10.0};
    ray.direction = {-1.0, 0.0, 0.0};

    const auto hit = pickFace(cylinder, ray);
    REQUIRE(hit.has_value());
    REQUIRE(hit->point[0] == Approx(5.0).margin(1e-6));
    REQUIRE(hit->distance == Approx(95.0).margin(1e-6));
    // The outward radial normal at (5,0,10) on a cylinder centered on the Z axis is +X.
    REQUIRE(hit->normal[0] == Approx(1.0).margin(1e-3));
}

TEST_CASE("pickEdge finds the nearest edge to a ray passing close to one corner", "[core3d][pick]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    // A ray running parallel to the vertical edge at (10,10,z), offset by 0.1.
    PickRay ray;
    ray.origin = {10.1, 10.0, -50.0};
    ray.direction = {0.0, 0.0, 1.0};

    const auto hit = pickEdge(box, ray, 0.5);
    REQUIRE(hit.has_value());
    REQUIRE(hit->point[0] == Approx(10.0).margin(1e-6));
    REQUIRE(hit->point[1] == Approx(10.0).margin(1e-6));
    REQUIRE(hit->distance == Approx(0.1).margin(1e-6));
}

TEST_CASE("pickEdge ignores an edge that lies behind the ray's own origin, matching pickFace's own w>=0 rule",
         "[core3d][pick]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    // The box's vertical edges span z in [0,10]. Placing the origin ABOVE
    // the box and pointing further upward (away from it) means every
    // point on that edge is behind the ray's own origin along direction,
    // even though the edge is very close to the underlying INFINITE line.
    PickRay ray;
    ray.origin = {10.1, 10.0, 50.0};
    ray.direction = {0.0, 0.0, 1.0};

    REQUIRE_FALSE(pickEdge(box, ray, 0.5).has_value());
}

TEST_CASE("pickEdge returns nullopt when nothing is within tolerance", "[core3d][pick]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    PickRay ray;
    ray.origin = {5.0, 5.0, -50.0}; // straight through the middle, nowhere near any edge
    ray.direction = {0.0, 0.0, 1.0};

    REQUIRE_FALSE(pickEdge(box, ray, 0.1).has_value());
}

TEST_CASE("pickFace/pickEdge handle a null shape and a zero-length direction gracefully", "[core3d][pick]") {
    PickRay ray;
    ray.origin = {0, 0, 0};
    ray.direction = {0, 0, 1};
    REQUIRE_FALSE(pickFace(TopoDS_Shape(), ray).has_value());
    REQUIRE_FALSE(pickEdge(TopoDS_Shape(), ray, 0.1).has_value());

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    PickRay degenerateRay;
    degenerateRay.direction = {0.0, 0.0, 0.0};
    REQUIRE_FALSE(pickFace(box, degenerateRay).has_value());
    REQUIRE_FALSE(pickEdge(box, degenerateRay, 0.1).has_value());
}
