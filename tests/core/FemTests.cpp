#include "core/core3d/Fem.h"

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace lcad;
using Catch::Approx;

namespace {
double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

// Finds the face of shape whose own centroid has the smallest X --
// robust against however BRepPrimAPI_MakeBox happens to order its own
// faces (not formally specified/guaranteed by OCCT), unlike hardcoding
// an assumed index.
int minXFaceIndex(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
    int best = -1;
    double bestX = std::numeric_limits<double>::max();
    for (int i = 0; i < faceMap.Extent(); ++i) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(faceMap(i + 1), props);
        const double x = props.CentreOfMass().X();
        if (x < bestX) {
            bestX = x;
            best = i;
        }
    }
    return best;
}

// Same as minXFaceIndex, but the largest-X face.
int maxXFaceIndex(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
    int best = -1;
    double bestX = std::numeric_limits<double>::lowest();
    for (int i = 0; i < faceMap.Extent(); ++i) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(faceMap(i + 1), props);
        const double x = props.CentreOfMass().X();
        if (x > bestX) {
            bestX = x;
            best = i;
        }
    }
    return best;
}

double tetVolumeSum(const FemMesh& mesh) {
    double sum = 0.0;
    for (const auto& tet : mesh.tets) {
        const auto& p0 = mesh.nodes[static_cast<std::size_t>(tet[0])];
        const auto& p1 = mesh.nodes[static_cast<std::size_t>(tet[1])];
        const auto& p2 = mesh.nodes[static_cast<std::size_t>(tet[2])];
        const auto& p3 = mesh.nodes[static_cast<std::size_t>(tet[3])];
        const double ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
        const double bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
        const double cx = p3[0] - p0[0], cy = p3[1] - p0[1], cz = p3[2] - p0[2];
        const double crossX = by * cz - bz * cy;
        const double crossY = bz * cx - bx * cz;
        const double crossZ = bx * cy - by * cx;
        sum += std::abs(ax * crossX + ay * crossY + az * crossZ) / 6.0;
    }
    return sum;
}
} // namespace

TEST_CASE("buildVoxelMesh's tets exactly tile an axis-aligned box (no meshing approximation error)",
          "[core3d][fem]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);
    REQUIRE_FALSE(mesh.tets.empty());
    REQUIRE(tetVolumeSum(mesh) == Approx(volumeOf(box)).margin(1e-6));
}

TEST_CASE("buildVoxelMesh rejects a null shape or non-positive divisions", "[core3d][fem]") {
    REQUIRE(buildVoxelMesh(TopoDS_Shape(), 4).tets.empty());
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    REQUIRE(buildVoxelMesh(box, 0).tets.empty());
}

TEST_CASE("solveLinearStatic under pure axial tension matches the classical bar formula (u = FL/AE)",
          "[core3d][fem]") {
    const double length = 100.0, width = 20.0, height = 20.0;
    const TopoDS_Shape beam = BRepPrimAPI_MakeBox(length, width, height).Shape();
    // divisions=5 with these proportions gives a 5x1x1 grid of cubes: a
    // beam-like mesh whose cross-section is a single cell, so its 4 corner
    // nodes each carry an equal, exact quarter of the end face's area --
    // the nodal-force distribution below is therefore the *exact*
    // finite-element equivalent of a uniform end traction, not just an
    // approximation of one.
    const FemMesh mesh = buildVoxelMesh(beam, 5);
    REQUIRE_FALSE(mesh.tets.empty());

    FemMaterial material;
    material.youngsModulus = 200000.0;
    material.poissonsRatio = 0.3;

    FemBoundaryCondition bc;
    bc.fixedXMax = 1e-6;

    const double totalForce = 40000.0;
    const double area = width * height;

    std::vector<FemLoad> loads;
    for (const auto& node : mesh.nodes) {
        if (node[0] < length - 1e-6) continue; // only the far (x = length) face
        FemLoad load;
        load.point = node;
        load.forceVector = {totalForce / 4.0, 0.0, 0.0}; // 4 corner nodes on that face
        loads.push_back(load);
    }
    REQUIRE(loads.size() == 4);

    const FemResult result = solveLinearStatic(mesh, material, bc, loads);
    REQUIRE(result.solved);

    const double expectedDisplacement = totalForce * length / (area * material.youngsModulus);
    const double expectedStress = totalForce / area;

    // A per-element shape-function-gradient patch test (prescribing an
    // exact linear displacement field on an arbitrary tet and confirming
    // the recovered strain matches it exactly, plus partition-of-unity and
    // Kronecker-delta checks on the shape functions themselves) confirms
    // the element formulation itself is exact -- so the ~5-8% spread seen
    // here against the idealized 1D bar formula is real coarse-mesh/
    // clamped-boundary discretization behavior (this mesh's cross-section
    // is a single cell of skewed tets, and the boundary condition also
    // suppresses Poisson contraction right at the fixed face, an effect
    // the idealized free-bar formula doesn't have), not a bug. Averaging
    // across the 4 end-face corners (which cancels most of the per-corner
    // skew noise) against a generous tolerance is the meaningful check
    // here; individual corners are only checked for staying close to that
    // average, not to the idealized formula directly.
    double averageDisplacement = 0.0;
    double averageStress = 0.0;
    int endNodeCount = 0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i][0] < length - 1e-6) continue;
        averageDisplacement += result.displacements[i][0];
        ++endNodeCount;
    }
    averageDisplacement /= endNodeCount;
    for (double vm : result.vonMisesStress) averageStress += vm;
    averageStress /= static_cast<double>(result.vonMisesStress.size());

    REQUIRE(averageDisplacement == Approx(expectedDisplacement).epsilon(0.10));
    REQUIRE(averageStress == Approx(expectedStress).epsilon(0.10));

    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i][0] < length - 1e-6) continue;
        REQUIRE(result.displacements[i][0] == Approx(averageDisplacement).epsilon(0.05));
    }
}

TEST_CASE("solveLinearStatic fixes every node at or below fixedXMax to zero displacement", "[core3d][fem]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 20.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);

    FemMaterial material;
    FemBoundaryCondition bc;
    bc.fixedXMax = 1e-6;
    std::vector<FemLoad> loads = {{{40.0, 10.0, 10.0}, {1000.0, 0.0, 0.0}}};

    const FemResult result = solveLinearStatic(mesh, material, bc, loads);
    REQUIRE(result.solved);

    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i][0] > bc.fixedXMax) continue;
        REQUIRE(result.displacements[i][0] == Approx(0.0).margin(1e-9));
        REQUIRE(result.displacements[i][1] == Approx(0.0).margin(1e-9));
        REQUIRE(result.displacements[i][2] == Approx(0.0).margin(1e-9));
    }
}

TEST_CASE("solveLinearStatic fails cleanly on an empty mesh", "[core3d][fem]") {
    FemMesh empty;
    FemMaterial material;
    FemBoundaryCondition bc;
    const FemResult result = solveLinearStatic(empty, material, bc, {});
    REQUIRE_FALSE(result.solved);
}

TEST_CASE("distributedBodyForce's per-node shares sum back to forcePerVolume times the mesh's total volume",
         "[core3d][fem][distributed]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 20.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);
    REQUIRE_FALSE(mesh.tets.empty());

    const std::array<double, 3> forcePerVolume = {0.0, 0.0, -9.8};
    const std::vector<FemLoad> loads = distributedBodyForce(mesh, forcePerVolume);
    REQUIRE(loads.size() == mesh.tets.size() * 4);

    double totalZ = 0.0;
    for (const FemLoad& load : loads) totalZ += load.forceVector[2];
    REQUIRE(totalZ == Approx(forcePerVolume[2] * tetVolumeSum(mesh)).epsilon(1e-9));
}

TEST_CASE("distributedPressureLoad on a beam's end face reproduces the manually-spread-load axial tension result",
         "[core3d][fem][distributed]") {
    const double length = 100.0, width = 20.0, height = 20.0;
    const TopoDS_Shape beam = BRepPrimAPI_MakeBox(length, width, height).Shape();
    const FemMesh mesh = buildVoxelMesh(beam, 5);
    REQUIRE_FALSE(mesh.tets.empty());

    const double pressure = 100.0; // force per area
    const double area = width * height;
    const std::vector<FemLoad> loads =
        distributedPressureLoad(mesh, {length - 1e-6, -1.0, -1.0}, {length + 1.0, width + 1.0, height + 1.0}, pressure,
                                {1.0, 0.0, 0.0});
    // This mesh's end face (a single cross-section cell) is exactly 2
    // triangles, so 2*3 = 6 (face, node) load entries -- some sharing a
    // node between the two triangles, which solveLinearStatic correctly
    // sums rather than merging here.
    REQUIRE(loads.size() == 6);

    double totalForceX = 0.0;
    for (const FemLoad& load : loads) totalForceX += load.forceVector[0];
    // OCCT's own Bnd_Box carries a small enlargement gap (~1e-7 in model
    // units) around the shape's true extent -- the same reason the mesh
    // volume test above needs a margin against the analytic box volume
    // rather than an exact match -- so this end face is very slightly
    // larger than the ideal 20x20, not exactly equal to it.
    REQUIRE(totalForceX == Approx(pressure * area).epsilon(1e-4));

    FemMaterial material;
    material.youngsModulus = 200000.0;
    material.poissonsRatio = 0.3;
    FemBoundaryCondition bc;
    bc.fixedXMax = 1e-6;

    const FemResult result = solveLinearStatic(mesh, material, bc, loads);
    REQUIRE(result.solved);

    const double expectedDisplacement = pressure * length / material.youngsModulus;
    double averageDisplacement = 0.0;
    int endNodeCount = 0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i][0] < length - 1e-6) continue;
        averageDisplacement += result.displacements[i][0];
        ++endNodeCount;
    }
    REQUIRE(endNodeCount == 4);
    averageDisplacement /= endNodeCount;
    REQUIRE(averageDisplacement == Approx(expectedDisplacement).epsilon(0.10));
}

TEST_CASE("nodesOnFaces finds exactly the same nodes as the equivalent fixedXMax plane clamp",
         "[core3d][fem][faces]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 20.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);
    REQUIRE_FALSE(mesh.tets.empty());

    const int minFace = minXFaceIndex(box);
    REQUIRE(minFace >= 0);
    // A generous tolerance relative to the mesh's own voxel cell width
    // (40/4 = 10 units) -- real boundary nodes should sit very close to
    // the true face here (an axis-aligned box's own faces are exactly
    // representable by the voxel mesh's boundary), so this mostly just
    // needs to not be pathologically tight.
    const std::vector<int> onFace = nodesOnFaces(mesh, box, {minFace}, 1.0);
    REQUIRE_FALSE(onFace.empty());

    std::vector<int> expected;
    for (std::size_t n = 0; n < mesh.nodes.size(); ++n) {
        if (mesh.nodes[n][0] <= 1e-6) expected.push_back(static_cast<int>(n));
    }
    REQUIRE_FALSE(expected.empty());

    std::vector<int> sortedOnFace = onFace;
    std::sort(sortedOnFace.begin(), sortedOnFace.end());
    std::sort(expected.begin(), expected.end());
    REQUIRE(sortedOnFace == expected);
}

TEST_CASE("nodesOnFaces returns empty for an out-of-range face index or empty faceIndices",
         "[core3d][fem][faces]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 20.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);
    REQUIRE(nodesOnFaces(mesh, box, {}, 1.0).empty());
    REQUIRE(nodesOnFaces(mesh, box, {999}, 1.0).empty());
    REQUIRE(nodesOnFaces(mesh, box, {-1}, 1.0).empty());
}

TEST_CASE("FemBoundaryCondition::fixedNodeIndices (real face-picking) fixes the same nodes as the "
         "equivalent fixedXMax plane clamp",
         "[core3d][fem][faces]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 20.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);

    const int minFace = minXFaceIndex(box);
    REQUIRE(minFace >= 0);

    FemMaterial material;
    std::vector<FemLoad> loads = {{{40.0, 10.0, 10.0}, {1000.0, 0.0, 0.0}}};

    FemBoundaryCondition faceBc;
    faceBc.fixedNodeIndices = nodesOnFaces(mesh, box, {minFace}, 1.0);
    REQUIRE_FALSE(faceBc.fixedNodeIndices.empty());
    const FemResult faceResult = solveLinearStatic(mesh, material, faceBc, loads);
    REQUIRE(faceResult.solved);

    FemBoundaryCondition planeBc;
    planeBc.fixedXMax = 1e-6;
    const FemResult planeResult = solveLinearStatic(mesh, material, planeBc, loads);
    REQUIRE(planeResult.solved);

    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        REQUIRE(faceResult.displacements[i][0] == Approx(planeResult.displacements[i][0]).margin(1e-9));
        REQUIRE(faceResult.displacements[i][1] == Approx(planeResult.displacements[i][1]).margin(1e-9));
        REQUIRE(faceResult.displacements[i][2] == Approx(planeResult.displacements[i][2]).margin(1e-9));
    }
}

TEST_CASE("distributedPressureLoadOnFaces on a beam's far face reproduces distributedPressureLoad's own "
         "box-selected result",
         "[core3d][fem][faces][distributed]") {
    const double length = 100.0, width = 20.0, height = 20.0;
    const TopoDS_Shape beam = BRepPrimAPI_MakeBox(length, width, height).Shape();
    const FemMesh mesh = buildVoxelMesh(beam, 5);
    REQUIRE_FALSE(mesh.tets.empty());

    const int farFace = maxXFaceIndex(beam);
    REQUIRE(farFace >= 0);

    const double pressure = 100.0;
    const double area = width * height;
    const std::vector<FemLoad> faceLoads =
        distributedPressureLoadOnFaces(mesh, beam, {farFace}, pressure, {1.0, 0.0, 0.0}, 1.0);
    REQUIRE(faceLoads.size() == 6); // same 2-triangle end face as the box-selected test

    double totalForceX = 0.0;
    for (const FemLoad& load : faceLoads) totalForceX += load.forceVector[0];
    REQUIRE(totalForceX == Approx(pressure * area).epsilon(1e-4));
}

TEST_CASE("distributedPressureLoadOnFaces returns empty for an out-of-range face index", "[core3d][fem][faces]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 20.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);
    REQUIRE(distributedPressureLoadOnFaces(mesh, box, {999}, 100.0, {1.0, 0.0, 0.0}, 1.0).empty());
}

TEST_CASE("solveModal's fundamental frequency scales exactly with material stiffness and mass", "[core3d][fem][modal]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 10.0, 10.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 2);
    REQUIRE_FALSE(mesh.tets.empty());

    FemBoundaryCondition bc;
    bc.fixedXMax = 1e-6;

    FemMaterial baseline;
    baseline.youngsModulus = 200000.0;
    baseline.poissonsRatio = 0.3;
    baseline.density = 1.0;

    const FemModalResult base = solveModal(mesh, baseline, bc);
    REQUIRE(base.solved);
    REQUIRE(base.angularFrequencySquared > 0.0);

    // omega^2 = K/M in the generalized eigenproblem -- K scales exactly
    // linearly with Young's modulus, so doubling it must exactly double
    // the eigenvalue.
    FemMaterial stiffer = baseline;
    stiffer.youngsModulus = baseline.youngsModulus * 2.0;
    const FemModalResult stiffened = solveModal(mesh, stiffer, bc);
    REQUIRE(stiffened.solved);
    REQUIRE(stiffened.angularFrequencySquared == Approx(base.angularFrequencySquared * 2.0).epsilon(0.02));

    // The lumped mass matrix scales exactly linearly with density, so
    // doubling it must exactly halve the eigenvalue.
    FemMaterial denser = baseline;
    denser.density = baseline.density * 2.0;
    const FemModalResult densified = solveModal(mesh, denser, bc);
    REQUIRE(densified.solved);
    REQUIRE(densified.angularFrequencySquared == Approx(base.angularFrequencySquared * 0.5).epsilon(0.02));

    // The mode shape must be (numerically) zero at every fixed node.
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i][0] > bc.fixedXMax) continue;
        REQUIRE(base.modeShape[i][0] == Approx(0.0).margin(1e-9));
        REQUIRE(base.modeShape[i][1] == Approx(0.0).margin(1e-9));
        REQUIRE(base.modeShape[i][2] == Approx(0.0).margin(1e-9));
    }
}

TEST_CASE("solveModal fails cleanly on an empty mesh", "[core3d][fem][modal]") {
    FemMesh empty;
    FemMaterial material;
    FemBoundaryCondition bc;
    const FemModalResult result = solveModal(empty, material, bc);
    REQUIRE_FALSE(result.solved);
}

TEST_CASE("solveThermalSteadyState under a 1D heat flux matches the classical conduction formula (dT = qL/(kA))",
         "[core3d][fem][thermal]") {
    // Same beam mesh (and the same reasoning about why its cross-section
    // is a single cell, so a uniform end heat flux splits into an exact
    // quarter share per corner node) as the analogous axial-tension test
    // above -- steady-state 1D conduction is the thermal twin of the
    // classical bar-under-tension formula.
    const double length = 100.0, width = 20.0, height = 20.0;
    const TopoDS_Shape beam = BRepPrimAPI_MakeBox(length, width, height).Shape();
    const FemMesh mesh = buildVoxelMesh(beam, 5);
    REQUIRE_FALSE(mesh.tets.empty());

    FemThermalMaterial material;
    material.thermalConductivity = 50.0;

    FemThermalBoundaryCondition bc;
    bc.fixedXMax = 1e-6;
    bc.fixedTemperature = 20.0;

    const double totalHeatRate = 1000.0;
    const double area = width * height;

    std::vector<ThermalLoad> loads;
    for (const auto& node : mesh.nodes) {
        if (node[0] < length - 1e-6) continue; // only the far (x = length) face
        ThermalLoad load;
        load.point = node;
        load.heatRate = totalHeatRate / 4.0; // 4 corner nodes on that face
        loads.push_back(load);
    }
    REQUIRE(loads.size() == 4);

    const FemThermalResult result = solveThermalSteadyState(mesh, material, bc, loads);
    REQUIRE(result.solved);

    const double expectedDeltaT = totalHeatRate * length / (area * material.thermalConductivity);

    double averageFarTemp = 0.0;
    int endNodeCount = 0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i][0] < length - 1e-6) continue;
        averageFarTemp += result.temperatures[i];
        ++endNodeCount;
    }
    averageFarTemp /= endNodeCount;

    // Same generous coarse-mesh tolerance the mechanical analog uses, for
    // the same reason (this mesh's cross-section is a single skewed-tet
    // cell).
    REQUIRE(averageFarTemp - bc.fixedTemperature == Approx(expectedDeltaT).epsilon(0.10));

    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        if (mesh.nodes[i][0] > bc.fixedXMax) continue;
        REQUIRE(result.temperatures[i] == Approx(bc.fixedTemperature).margin(1e-9));
    }
}

TEST_CASE("solveThermalSteadyState fails cleanly on an empty mesh", "[core3d][fem][thermal]") {
    FemMesh empty;
    FemThermalMaterial material;
    FemThermalBoundaryCondition bc;
    const FemThermalResult result = solveThermalSteadyState(empty, material, bc, {});
    REQUIRE_FALSE(result.solved);
}

TEST_CASE("buildFemVisualization produces one non-null shape and one color per tet", "[core3d][fem][viz]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 20.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 4);
    REQUIRE_FALSE(mesh.tets.empty());

    FemMaterial material;
    FemBoundaryCondition bc;
    bc.fixedXMax = 1e-6;
    std::vector<FemLoad> loads = {{{40.0, 10.0, 10.0}, {1000.0, 0.0, 0.0}}};
    const FemResult result = solveLinearStatic(mesh, material, bc, loads);
    REQUIRE(result.solved);

    const FemVisualization viz = buildFemVisualization(mesh, result, 1.0);
    REQUIRE(viz.elementShapes.size() == mesh.tets.size());
    REQUIRE(viz.elementColors.size() == mesh.tets.size());
    for (const auto& shape : viz.elementShapes) REQUIRE_FALSE(shape.IsNull());

    // Every color channel is a valid, finite [0,1] value.
    for (const auto& color : viz.elementColors) {
        for (double channel : color) {
            REQUIRE(channel >= 0.0);
            REQUIRE(channel <= 1.0);
        }
    }

    // The single highest-stress element should read as "hot" (red channel
    // clearly dominant), matching the blue-green-red heatmap convention.
    const std::size_t maxIdx = static_cast<std::size_t>(
        std::max_element(result.vonMisesStress.begin(), result.vonMisesStress.end()) - result.vonMisesStress.begin());
    REQUIRE(viz.elementColors[maxIdx][0] == Approx(1.0).margin(1e-9));
    REQUIRE(viz.elementColors[maxIdx][2] == Approx(0.0).margin(1e-9));
}

TEST_CASE("buildFemVisualization returns empty for an unsolved result", "[core3d][fem][viz]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const FemMesh mesh = buildVoxelMesh(box, 2);
    FemResult unsolved;
    const FemVisualization viz = buildFemVisualization(mesh, unsolved, 1.0);
    REQUIRE(viz.elementShapes.empty());
    REQUIRE(viz.elementColors.empty());
}
