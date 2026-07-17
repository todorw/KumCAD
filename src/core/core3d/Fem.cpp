#include "core/core3d/Fem.h"

#include "core/sketch/LinearSolve.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace lcad {

namespace {

double tetVolume(const std::array<double, 3>& p0, const std::array<double, 3>& p1, const std::array<double, 3>& p2,
                  const std::array<double, 3>& p3) {
    const double ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
    const double bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
    const double cx = p3[0] - p0[0], cy = p3[1] - p0[1], cz = p3[2] - p0[2];
    const double crossX = by * cz - bz * cy;
    const double crossY = bz * cx - bx * cz;
    const double crossZ = bx * cy - by * cx;
    return std::abs(ax * crossX + ay * crossY + az * crossZ) / 6.0;
}

// Shape function coefficients (a,b,c,d in N = a + b*x + c*y + d*z) for
// every one of the 4 nodes, found by solving the defining 4x4 system
// directly (N_i == 1 at node i, 0 at the others) rather than hand-deriving
// cofactor-expansion formulas -- see Fem.h's own note on why.
bool tetShapeFunctionCoeffs(const std::array<std::array<double, 3>, 4>& p, std::array<std::array<double, 4>, 4>& coeffs) {
    std::vector<std::vector<double>> m = {
        {1.0, p[0][0], p[0][1], p[0][2]},
        {1.0, p[1][0], p[1][1], p[1][2]},
        {1.0, p[2][0], p[2][1], p[2][2]},
        {1.0, p[3][0], p[3][1], p[3][2]},
    };
    for (int i = 0; i < 4; ++i) {
        std::vector<double> rhs(4, 0.0);
        rhs[static_cast<std::size_t>(i)] = 1.0;
        std::vector<double> solution;
        if (!solveLinearSystem(m, rhs, solution)) return false;
        for (int j = 0; j < 4; ++j) coeffs[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = solution[static_cast<std::size_t>(j)];
    }
    return true;
}

// 6x6 isotropic elasticity matrix relating strain [exx,eyy,ezz,gxy,gyz,gzx]
// to stress [sxx,syy,szz,txy,tyz,tzx].
std::vector<std::vector<double>> materialMatrix(const FemMaterial& material) {
    const double e = material.youngsModulus;
    const double nu = material.poissonsRatio;
    const double factor = e / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double shear = (1.0 - 2.0 * nu) / 2.0;
    return {
        {factor * (1 - nu), factor * nu, factor * nu, 0, 0, 0},
        {factor * nu, factor * (1 - nu), factor * nu, 0, 0, 0},
        {factor * nu, factor * nu, factor * (1 - nu), 0, 0, 0},
        {0, 0, 0, factor * shear, 0, 0},
        {0, 0, 0, 0, factor * shear, 0},
        {0, 0, 0, 0, 0, factor * shear},
    };
}

// 6x12 strain-displacement matrix for one CST element, from its 4 nodes'
// shape-function gradients (b_i, c_i, d_i).
std::vector<std::vector<double>> strainDisplacementMatrix(const std::array<std::array<double, 4>, 4>& coeffs) {
    std::vector<std::vector<double>> b(6, std::vector<double>(12, 0.0));
    for (int i = 0; i < 4; ++i) {
        const double bi = coeffs[static_cast<std::size_t>(i)][1];
        const double ci = coeffs[static_cast<std::size_t>(i)][2];
        const double di = coeffs[static_cast<std::size_t>(i)][3];
        const int col = i * 3;
        b[0][static_cast<std::size_t>(col + 0)] = bi;
        b[1][static_cast<std::size_t>(col + 1)] = ci;
        b[2][static_cast<std::size_t>(col + 2)] = di;
        b[3][static_cast<std::size_t>(col + 0)] = ci;
        b[3][static_cast<std::size_t>(col + 1)] = bi;
        b[4][static_cast<std::size_t>(col + 1)] = di;
        b[4][static_cast<std::size_t>(col + 2)] = ci;
        b[5][static_cast<std::size_t>(col + 0)] = di;
        b[5][static_cast<std::size_t>(col + 2)] = bi;
    }
    return b;
}

std::vector<std::vector<double>> transposed(const std::vector<std::vector<double>>& m) {
    std::vector<std::vector<double>> t(m[0].size(), std::vector<double>(m.size()));
    for (std::size_t i = 0; i < m.size(); ++i)
        for (std::size_t j = 0; j < m[i].size(); ++j) t[j][i] = m[i][j];
    return t;
}

std::vector<std::vector<double>> multiplied(const std::vector<std::vector<double>>& a,
                                             const std::vector<std::vector<double>>& b) {
    std::vector<std::vector<double>> result(a.size(), std::vector<double>(b[0].size(), 0.0));
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t k = 0; k < b.size(); ++k) {
            const double aik = a[i][k];
            if (aik == 0.0) continue;
            for (std::size_t j = 0; j < b[0].size(); ++j) result[i][j] += aik * b[k][j];
        }
    return result;
}

std::vector<double> multipliedVec(const std::vector<std::vector<double>>& a, const std::vector<double>& v) {
    std::vector<double> result(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < v.size(); ++j) result[i] += a[i][j] * v[j];
    return result;
}

} // namespace

FemMesh buildVoxelMesh(const TopoDS_Shape& shape, int divisions) {
    FemMesh mesh;
    if (shape.IsNull() || divisions < 1) return mesh;

    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
    if (dx <= 1e-9 || dy <= 1e-9 || dz <= 1e-9) return mesh;

    const double cell = std::max({dx, dy, dz}) / static_cast<double>(divisions);
    const int nx = std::max(1, static_cast<int>(std::lround(dx / cell)));
    const int ny = std::max(1, static_cast<int>(std::lround(dy / cell)));
    const int nz = std::max(1, static_cast<int>(std::lround(dz / cell)));

    auto gridIndex = [&](int i, int j, int k) { return (i * (ny + 1) + j) * (nz + 1) + k; };
    std::vector<std::array<double, 3>> gridPoints(static_cast<std::size_t>((nx + 1) * (ny + 1) * (nz + 1)));
    for (int i = 0; i <= nx; ++i)
        for (int j = 0; j <= ny; ++j)
            for (int k = 0; k <= nz; ++k)
                gridPoints[static_cast<std::size_t>(gridIndex(i, j, k))] = {
                    xmin + dx * i / nx, ymin + dy * j / ny, zmin + dz * k / nz};

    BRepClass3d_SolidClassifier classifier(shape);
    constexpr double kTol = 1e-6;
    std::unordered_map<int, int> gridToMeshNode;
    auto getOrAddNode = [&](int gridIdx) {
        auto it = gridToMeshNode.find(gridIdx);
        if (it != gridToMeshNode.end()) return it->second;
        const int newIdx = static_cast<int>(mesh.nodes.size());
        mesh.nodes.push_back(gridPoints[static_cast<std::size_t>(gridIdx)]);
        gridToMeshNode.emplace(gridIdx, newIdx);
        return newIdx;
    };

    // The standard 6-tet decomposition of a cube sharing its main diagonal
    // (corner 0 = (0,0,0) to corner 7 = (1,1,1) in the bit-indexed corner
    // numbering below).
    static constexpr int kTetCorners[6][4] = {{0, 1, 3, 7}, {0, 3, 2, 7}, {0, 2, 6, 7},
                                              {0, 6, 4, 7}, {0, 4, 5, 7}, {0, 5, 1, 7}};

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                // Always sharing the SAME physical diagonal (corner 0 to
                // corner 7) across every cube gives the whole mesh a
                // directional stiffness bias -- a real, well-known artifact
                // of naive structured tet meshing, confirmed here by a
                // symmetric axial-tension test producing asymmetric corner
                // displacements even with Poisson's ratio set to 0 (so it
                // wasn't a Poisson/boundary effect). The standard fix is to
                // alternate which diagonal is shared in a 3D checkerboard
                // pattern; mirroring the Z-extent mapping for odd-parity
                // cubes below achieves exactly that without needing to
                // hand-derive a second hexagonal corner ordering.
                const bool flip = ((i + j + k) % 2) != 0;
                std::array<int, 8> corners{};
                int c = 0;
                for (int di = 0; di < 2; ++di)
                    for (int dj = 0; dj < 2; ++dj)
                        for (int dk = 0; dk < 2; ++dk) {
                            const int actualDk = flip ? (1 - dk) : dk;
                            corners[static_cast<std::size_t>(c++)] = gridIndex(i + di, j + dj, k + actualDk);
                        }

                for (const auto& tc : kTetCorners) {
                    const auto& p0 = gridPoints[static_cast<std::size_t>(corners[static_cast<std::size_t>(tc[0])])];
                    const auto& p1 = gridPoints[static_cast<std::size_t>(corners[static_cast<std::size_t>(tc[1])])];
                    const auto& p2 = gridPoints[static_cast<std::size_t>(corners[static_cast<std::size_t>(tc[2])])];
                    const auto& p3 = gridPoints[static_cast<std::size_t>(corners[static_cast<std::size_t>(tc[3])])];
                    const std::array<double, 3> centroid = {(p0[0] + p1[0] + p2[0] + p3[0]) / 4.0,
                                                             (p0[1] + p1[1] + p2[1] + p3[1]) / 4.0,
                                                             (p0[2] + p1[2] + p2[2] + p3[2]) / 4.0};
                    classifier.Perform(gp_Pnt(centroid[0], centroid[1], centroid[2]), kTol);
                    if (classifier.State() != TopAbs_IN) continue;

                    mesh.tets.push_back({getOrAddNode(corners[static_cast<std::size_t>(tc[0])]),
                                         getOrAddNode(corners[static_cast<std::size_t>(tc[1])]),
                                         getOrAddNode(corners[static_cast<std::size_t>(tc[2])]),
                                         getOrAddNode(corners[static_cast<std::size_t>(tc[3])])});
                }
            }
        }
    }
    return mesh;
}

// Assembles the global (unconstrained -- no boundary condition applied
// yet) stiffness matrix, shared by solveLinearStatic and solveModal.
// Also fills perTetCoeffs/perTetVolume (needed afterwards for stress
// recovery), and per-node lumped mass diag if massDiag is non-null (each
// tet's density*volume split evenly across its own 4 nodes, matching
// distributedBodyForce's own consistent-load idea, just for inertia).
// Returns an empty matrix if any tet is degenerate (zero volume).
std::vector<std::vector<double>> assembleStiffness(const FemMesh& mesh, const FemMaterial& material,
                                                    std::vector<std::array<std::array<double, 4>, 4>>& perTetCoeffs,
                                                    std::vector<double>& perTetVolume,
                                                    std::vector<double>* massDiag) {
    const std::size_t numNodes = mesh.nodes.size();
    const std::size_t numDofs = numNodes * 3;
    std::vector<std::vector<double>> stiffness(numDofs, std::vector<double>(numDofs, 0.0));
    if (massDiag) massDiag->assign(numDofs, 0.0);

    const std::vector<std::vector<double>> materialD = materialMatrix(material);

    perTetCoeffs.resize(mesh.tets.size());
    perTetVolume.resize(mesh.tets.size());

    for (std::size_t t = 0; t < mesh.tets.size(); ++t) {
        const auto& tet = mesh.tets[t];
        const std::array<std::array<double, 3>, 4> pts = {mesh.nodes[static_cast<std::size_t>(tet[0])],
                                                            mesh.nodes[static_cast<std::size_t>(tet[1])],
                                                            mesh.nodes[static_cast<std::size_t>(tet[2])],
                                                            mesh.nodes[static_cast<std::size_t>(tet[3])]};
        std::array<std::array<double, 4>, 4> coeffs{};
        if (!tetShapeFunctionCoeffs(pts, coeffs)) return {}; // a degenerate (zero-volume) tet
        perTetCoeffs[t] = coeffs;
        perTetVolume[t] = tetVolume(pts[0], pts[1], pts[2], pts[3]);

        const std::vector<std::vector<double>> b = strainDisplacementMatrix(coeffs);
        const std::vector<std::vector<double>> bt = transposed(b);
        std::vector<std::vector<double>> ke = multiplied(multiplied(bt, materialD), b);
        for (auto& row : ke)
            for (double& v : row) v *= perTetVolume[t];

        for (int a = 0; a < 4; ++a) {
            for (int b2 = 0; b2 < 4; ++b2) {
                for (int r = 0; r < 3; ++r) {
                    for (int cIdx = 0; cIdx < 3; ++cIdx) {
                        const std::size_t globalRow = static_cast<std::size_t>(tet[static_cast<std::size_t>(a)]) * 3 + static_cast<std::size_t>(r);
                        const std::size_t globalCol = static_cast<std::size_t>(tet[static_cast<std::size_t>(b2)]) * 3 + static_cast<std::size_t>(cIdx);
                        stiffness[globalRow][globalCol] += ke[static_cast<std::size_t>(a * 3 + r)][static_cast<std::size_t>(b2 * 3 + cIdx)];
                    }
                }
            }
        }

        if (massDiag) {
            const double share = material.density * perTetVolume[t] / 4.0;
            for (int a = 0; a < 4; ++a) {
                for (int d = 0; d < 3; ++d) {
                    (*massDiag)[static_cast<std::size_t>(tet[static_cast<std::size_t>(a)]) * 3 + static_cast<std::size_t>(d)] += share;
                }
            }
        }
    }
    return stiffness;
}

std::vector<FemLoad> distributedBodyForce(const FemMesh& mesh, const std::array<double, 3>& forcePerVolume) {
    std::vector<FemLoad> loads;
    loads.reserve(mesh.tets.size() * 4);
    for (const auto& tet : mesh.tets) {
        const auto& p0 = mesh.nodes[static_cast<std::size_t>(tet[0])];
        const auto& p1 = mesh.nodes[static_cast<std::size_t>(tet[1])];
        const auto& p2 = mesh.nodes[static_cast<std::size_t>(tet[2])];
        const auto& p3 = mesh.nodes[static_cast<std::size_t>(tet[3])];
        const double share = tetVolume(p0, p1, p2, p3) / 4.0;
        for (int a : tet) {
            loads.push_back({mesh.nodes[static_cast<std::size_t>(a)],
                             {forcePerVolume[0] * share, forcePerVolume[1] * share, forcePerVolume[2] * share}});
        }
    }
    return loads;
}

std::vector<FemLoad> distributedPressureLoad(const FemMesh& mesh, const std::array<double, 3>& boxMin,
                                             const std::array<double, 3>& boxMax, double pressure,
                                             const std::array<double, 3>& direction) {
    std::vector<FemLoad> loads;
    const double dirLen = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (dirLen < 1e-12) return loads;
    const std::array<double, 3> dir = {direction[0] / dirLen, direction[1] / dirLen, direction[2] / dirLen};

    // A tet's 4 triangular faces, as local corner indices. Counting how
    // many tets each (sorted) face's node triple appears in tells apart
    // boundary faces (exactly one owning tet) from interior ones (shared
    // by exactly two) -- the standard way to recover a tet mesh's own
    // boundary surface without a separate conforming-surface-mesh
    // structure.
    static constexpr int kLocalFaces[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    std::map<std::array<int, 3>, int> faceCount;
    std::map<std::array<int, 3>, std::array<int, 3>> faceNodes;
    for (const auto& tet : mesh.tets) {
        for (const auto& lf : kLocalFaces) {
            std::array<int, 3> nodesIdx = {tet[static_cast<std::size_t>(lf[0])], tet[static_cast<std::size_t>(lf[1])],
                                          tet[static_cast<std::size_t>(lf[2])]};
            std::array<int, 3> key = nodesIdx;
            std::sort(key.begin(), key.end());
            const int count = ++faceCount[key];
            if (count == 1) faceNodes[key] = nodesIdx;
        }
    }

    for (const auto& [key, count] : faceCount) {
        if (count != 1) continue; // interior face, shared by two tets
        const auto& nodesIdx = faceNodes.at(key);
        const auto& p0 = mesh.nodes[static_cast<std::size_t>(nodesIdx[0])];
        const auto& p1 = mesh.nodes[static_cast<std::size_t>(nodesIdx[1])];
        const auto& p2 = mesh.nodes[static_cast<std::size_t>(nodesIdx[2])];
        const std::array<double, 3> centroid = {(p0[0] + p1[0] + p2[0]) / 3.0, (p0[1] + p1[1] + p2[1]) / 3.0,
                                                (p0[2] + p1[2] + p2[2]) / 3.0};
        if (centroid[0] < boxMin[0] || centroid[0] > boxMax[0] || centroid[1] < boxMin[1] || centroid[1] > boxMax[1] ||
            centroid[2] < boxMin[2] || centroid[2] > boxMax[2]) {
            continue;
        }

        const double ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
        const double bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
        const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
        const double area = 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
        const double share = pressure * area / 3.0;

        for (int idx : nodesIdx) {
            loads.push_back({mesh.nodes[static_cast<std::size_t>(idx)], {dir[0] * share, dir[1] * share, dir[2] * share}});
        }
    }
    return loads;
}

FemResult solveLinearStatic(const FemMesh& mesh, const FemMaterial& material,
                            const FemBoundaryCondition& boundaryCondition, const std::vector<FemLoad>& loads) {
    FemResult result;
    if (mesh.tets.empty() || mesh.nodes.empty()) return result;

    const std::size_t numNodes = mesh.nodes.size();
    const std::size_t numDofs = numNodes * 3;

    std::vector<std::array<std::array<double, 4>, 4>> perTetCoeffs;
    std::vector<double> perTetVolume;
    std::vector<std::vector<double>> stiffness = assembleStiffness(mesh, material, perTetCoeffs, perTetVolume, nullptr);
    if (stiffness.empty()) return result;

    std::vector<double> force(numDofs, 0.0);

    for (const FemLoad& load : loads) {
        int nearest = -1;
        double bestDist = 0.0;
        for (std::size_t n = 0; n < numNodes; ++n) {
            const auto& p = mesh.nodes[n];
            const double dx = p[0] - load.point[0], dy = p[1] - load.point[1], dz = p[2] - load.point[2];
            const double dist = dx * dx + dy * dy + dz * dz;
            if (nearest < 0 || dist < bestDist) {
                nearest = static_cast<int>(n);
                bestDist = dist;
            }
        }
        if (nearest < 0) continue;
        force[static_cast<std::size_t>(nearest) * 3 + 0] += load.forceVector[0];
        force[static_cast<std::size_t>(nearest) * 3 + 1] += load.forceVector[1];
        force[static_cast<std::size_t>(nearest) * 3 + 2] += load.forceVector[2];
    }

    for (std::size_t n = 0; n < numNodes; ++n) {
        if (mesh.nodes[n][0] > boundaryCondition.fixedXMax) continue;
        for (int d = 0; d < 3; ++d) {
            const std::size_t dof = n * 3 + static_cast<std::size_t>(d);
            for (std::size_t col = 0; col < numDofs; ++col) stiffness[dof][col] = 0.0;
            for (std::size_t row = 0; row < numDofs; ++row) stiffness[row][dof] = 0.0;
            stiffness[dof][dof] = 1.0;
            force[dof] = 0.0;
        }
    }

    std::vector<double> displacementVec;
    if (!solveLinearSystem(stiffness, force, displacementVec)) return result;

    result.displacements.resize(numNodes);
    for (std::size_t n = 0; n < numNodes; ++n) {
        result.displacements[n] = {displacementVec[n * 3 + 0], displacementVec[n * 3 + 1], displacementVec[n * 3 + 2]};
    }

    const std::vector<std::vector<double>> materialD = materialMatrix(material);
    result.vonMisesStress.resize(mesh.tets.size());
    for (std::size_t t = 0; t < mesh.tets.size(); ++t) {
        const auto& tet = mesh.tets[t];
        std::vector<double> localDisp(12);
        for (int a = 0; a < 4; ++a) {
            for (int d = 0; d < 3; ++d) {
                localDisp[static_cast<std::size_t>(a * 3 + d)] = displacementVec[static_cast<std::size_t>(tet[static_cast<std::size_t>(a)]) * 3 + static_cast<std::size_t>(d)];
            }
        }
        const std::vector<std::vector<double>> b = strainDisplacementMatrix(perTetCoeffs[t]);
        const std::vector<double> strain = multipliedVec(b, localDisp);
        const std::vector<double> stress = multipliedVec(materialD, strain);
        const double sxx = stress[0], syy = stress[1], szz = stress[2];
        const double txy = stress[3], tyz = stress[4], tzx = stress[5];
        const double vm = std::sqrt(0.5 * ((sxx - syy) * (sxx - syy) + (syy - szz) * (syy - szz) + (szz - sxx) * (szz - sxx) +
                                           6.0 * (txy * txy + tyz * tyz + tzx * tzx)));
        result.vonMisesStress[t] = vm;
    }

    result.solved = true;
    return result;
}

FemModalResult solveModal(const FemMesh& mesh, const FemMaterial& material,
                          const FemBoundaryCondition& boundaryCondition, int maxIterations) {
    FemModalResult result;
    if (mesh.tets.empty() || mesh.nodes.empty()) return result;

    const std::size_t numNodes = mesh.nodes.size();
    const std::size_t numDofs = numNodes * 3;

    std::vector<std::array<std::array<double, 4>, 4>> perTetCoeffs;
    std::vector<double> perTetVolume;
    std::vector<double> massDiag;
    std::vector<std::vector<double>> stiffness = assembleStiffness(mesh, material, perTetCoeffs, perTetVolume, &massDiag);
    if (stiffness.empty()) return result;

    // Same boundary-condition treatment as solveLinearStatic's -- an
    // identity row/column in K for each fixed DOF -- plus zeroing that
    // DOF's lumped mass, which removes it from the eigenproblem entirely.
    for (std::size_t n = 0; n < numNodes; ++n) {
        if (mesh.nodes[n][0] > boundaryCondition.fixedXMax) continue;
        for (int d = 0; d < 3; ++d) {
            const std::size_t dof = n * 3 + static_cast<std::size_t>(d);
            for (std::size_t col = 0; col < numDofs; ++col) stiffness[dof][col] = 0.0;
            for (std::size_t row = 0; row < numDofs; ++row) stiffness[row][dof] = 0.0;
            stiffness[dof][dof] = 1.0;
            massDiag[dof] = 0.0;
        }
    }

    auto massNorm = [&](const std::vector<double>& v) {
        double sum = 0.0;
        for (std::size_t i = 0; i < numDofs; ++i) sum += massDiag[i] * v[i] * v[i];
        return std::sqrt(sum);
    };
    auto rayleighQuotient = [&](const std::vector<double>& v, const std::vector<double>& kv) {
        double numerator = 0.0, denominator = 0.0;
        for (std::size_t i = 0; i < numDofs; ++i) {
            numerator += v[i] * kv[i];
            denominator += massDiag[i] * v[i] * v[i];
        }
        return denominator > 1e-300 ? numerator / denominator : 0.0;
    };

    // Start from 1.0 at every free DOF (0 at fixed ones) -- any vector
    // with a nonzero component along the true fundamental mode works as
    // an inverse-iteration seed, and this one has no special symmetry
    // that would accidentally make it orthogonal to that mode.
    std::vector<double> x(numDofs, 1.0);
    for (std::size_t i = 0; i < numDofs; ++i) {
        if (massDiag[i] <= 0.0) x[i] = 0.0;
    }
    double norm = massNorm(x);
    if (norm < 1e-300) return result; // every DOF fixed -- nothing to vibrate
    for (double& v : x) v /= norm;

    double lambda = 0.0;
    for (int iter = 0; iter < maxIterations; ++iter) {
        std::vector<double> rhs(numDofs);
        for (std::size_t i = 0; i < numDofs; ++i) rhs[i] = massDiag[i] * x[i];

        std::vector<double> y;
        if (!solveLinearSystem(stiffness, rhs, y)) return result;

        const double newLambda = rayleighQuotient(y, multipliedVec(stiffness, y));

        const double yNorm = massNorm(y);
        if (yNorm < 1e-300) return result;
        for (double& v : y) v /= yNorm;

        // Converged once the Rayleigh quotient (the eigenvalue estimate)
        // stops moving -- inverse power iteration typically settles in a
        // handful of iterations for a well-separated fundamental mode, so
        // this usually exits long before maxIterations.
        const bool converged = iter > 0 && std::abs(newLambda - lambda) < 1e-9 * std::max(1.0, std::abs(newLambda));
        lambda = newLambda;
        x = y;
        if (converged) break;
    }

    result.angularFrequencySquared = lambda;
    result.modeShape.resize(numNodes);
    for (std::size_t n = 0; n < numNodes; ++n) result.modeShape[n] = {x[n * 3 + 0], x[n * 3 + 1], x[n * 3 + 2]};
    result.solved = true;
    return result;
}

FemThermalResult solveThermalSteadyState(const FemMesh& mesh, const FemThermalMaterial& material,
                                         const FemThermalBoundaryCondition& boundaryCondition,
                                         const std::vector<ThermalLoad>& loads) {
    FemThermalResult result;
    if (mesh.tets.empty() || mesh.nodes.empty()) return result;

    const std::size_t numNodes = mesh.nodes.size();
    // One scalar DOF (temperature) per node, unlike the 3-per-node
    // displacement field -- the conductivity matrix is exactly the
    // elasticity stiffness matrix's scalar-field analog: k * volume *
    // grad(N_i).grad(N_j), built from the same per-element shape-
    // function gradients (tetShapeFunctionCoeffs).
    std::vector<std::vector<double>> conductivity(numNodes, std::vector<double>(numNodes, 0.0));
    std::vector<double> heat(numNodes, 0.0);

    for (const auto& tet : mesh.tets) {
        const std::array<std::array<double, 3>, 4> pts = {mesh.nodes[static_cast<std::size_t>(tet[0])],
                                                            mesh.nodes[static_cast<std::size_t>(tet[1])],
                                                            mesh.nodes[static_cast<std::size_t>(tet[2])],
                                                            mesh.nodes[static_cast<std::size_t>(tet[3])]};
        std::array<std::array<double, 4>, 4> coeffs{};
        if (!tetShapeFunctionCoeffs(pts, coeffs)) return result; // a degenerate (zero-volume) tet
        const double volume = tetVolume(pts[0], pts[1], pts[2], pts[3]);

        for (int a = 0; a < 4; ++a) {
            for (int b = 0; b < 4; ++b) {
                const double dot = coeffs[static_cast<std::size_t>(a)][1] * coeffs[static_cast<std::size_t>(b)][1] +
                                  coeffs[static_cast<std::size_t>(a)][2] * coeffs[static_cast<std::size_t>(b)][2] +
                                  coeffs[static_cast<std::size_t>(a)][3] * coeffs[static_cast<std::size_t>(b)][3];
                conductivity[static_cast<std::size_t>(tet[static_cast<std::size_t>(a)])]
                            [static_cast<std::size_t>(tet[static_cast<std::size_t>(b)])] +=
                    material.thermalConductivity * volume * dot;
            }
        }
    }

    for (const ThermalLoad& load : loads) {
        int nearest = -1;
        double bestDist = 0.0;
        for (std::size_t n = 0; n < numNodes; ++n) {
            const auto& p = mesh.nodes[n];
            const double dx = p[0] - load.point[0], dy = p[1] - load.point[1], dz = p[2] - load.point[2];
            const double dist = dx * dx + dy * dy + dz * dz;
            if (nearest < 0 || dist < bestDist) {
                nearest = static_cast<int>(n);
                bestDist = dist;
            }
        }
        if (nearest >= 0) heat[static_cast<std::size_t>(nearest)] += load.heatRate;
    }

    // Unlike solveLinearStatic's boundary condition (always exactly 0
    // displacement, so simply zeroing a fixed DOF's row/column never
    // drops anything real), a non-zero prescribed temperature's coupling
    // into every OTHER equation has to be moved onto their own
    // right-hand side first -- using the matrix's still-unmodified
    // values -- or those equations would silently behave as if every
    // fixed node were held at 0 instead of fixedTemperature. This is the
    // standard non-homogeneous-Dirichlet FEM technique, done here rather
    // than solveLinearStatic ever needing it.
    std::vector<bool> isFixed(numNodes, false);
    for (std::size_t n = 0; n < numNodes; ++n) isFixed[n] = mesh.nodes[n][0] <= boundaryCondition.fixedXMax;

    if (boundaryCondition.fixedTemperature != 0.0) {
        for (std::size_t r = 0; r < numNodes; ++r) {
            if (isFixed[r]) continue;
            for (std::size_t n = 0; n < numNodes; ++n) {
                if (!isFixed[n]) continue;
                heat[r] -= conductivity[r][n] * boundaryCondition.fixedTemperature;
            }
        }
    }

    for (std::size_t n = 0; n < numNodes; ++n) {
        if (!isFixed[n]) continue;
        for (std::size_t col = 0; col < numNodes; ++col) conductivity[n][col] = 0.0;
        for (std::size_t row = 0; row < numNodes; ++row) conductivity[row][n] = 0.0;
        conductivity[n][n] = 1.0;
        heat[n] = boundaryCondition.fixedTemperature;
    }

    std::vector<double> temperatures;
    if (!solveLinearSystem(conductivity, heat, temperatures)) return result;

    result.temperatures = std::move(temperatures);
    result.solved = true;
    return result;
}

namespace {

TopoDS_Face makeTriFace(const gp_Pnt& a, const gp_Pnt& b, const gp_Pnt& c) {
    BRepBuilderAPI_MakePolygon poly;
    poly.Add(a);
    poly.Add(b);
    poly.Add(c);
    poly.Close();
    if (!poly.IsDone()) return TopoDS_Face();
    BRepBuilderAPI_MakeFace faceBuilder(poly.Wire());
    return faceBuilder.IsDone() ? faceBuilder.Face() : TopoDS_Face();
}

// A compound of the tet's 4 triangular faces -- not a true watertight
// solid, deliberately (see FemVisualization's own comment in Fem.h).
TopoDS_Shape buildTetShape(const gp_Pnt& p0, const gp_Pnt& p1, const gp_Pnt& p2, const gp_Pnt& p3) {
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const TopoDS_Face& face : {makeTriFace(p0, p1, p2), makeTriFace(p0, p2, p3), makeTriFace(p0, p3, p1),
                                    makeTriFace(p1, p3, p2)}) {
        if (!face.IsNull()) builder.Add(compound, face);
    }
    return compound;
}

// Blue (low) -> green -> red (high) -- t clamped to [0,1].
std::array<double, 3> heatmapColor(double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (t < 0.5) {
        const double s = t / 0.5;
        return {0.0, s, 1.0 - s};
    }
    const double s = (t - 0.5) / 0.5;
    return {s, 1.0 - s, 0.0};
}

} // namespace

FemVisualization buildFemVisualization(const FemMesh& mesh, const FemResult& result, double displacementScale) {
    FemVisualization viz;
    if (!result.solved || mesh.tets.empty()) return viz;

    double maxStress = 0.0;
    for (double vm : result.vonMisesStress) maxStress = std::max(maxStress, vm);
    if (maxStress <= 1e-12) maxStress = 1.0;

    for (std::size_t t = 0; t < mesh.tets.size(); ++t) {
        const auto& tet = mesh.tets[t];
        std::array<gp_Pnt, 4> corners;
        for (int i = 0; i < 4; ++i) {
            const auto& node = mesh.nodes[static_cast<std::size_t>(tet[static_cast<std::size_t>(i)])];
            const auto& disp = result.displacements[static_cast<std::size_t>(tet[static_cast<std::size_t>(i)])];
            corners[static_cast<std::size_t>(i)] = gp_Pnt(node[0] + disp[0] * displacementScale, node[1] + disp[1] * displacementScale,
                                                           node[2] + disp[2] * displacementScale);
        }
        viz.elementShapes.push_back(buildTetShape(corners[0], corners[1], corners[2], corners[3]));
        viz.elementColors.push_back(heatmapColor(result.vonMisesStress[t] / maxStress));
    }
    return viz;
}

} // namespace lcad
