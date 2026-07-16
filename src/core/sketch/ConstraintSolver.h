#pragma once

#include "core/sketch/SketchGeometry.h"

namespace lcad {

struct SolveResult {
    bool converged = false;
    double finalResidualNorm = 0.0;
    int iterations = 0;
};

// Solves sketch's constraints in place via Levenberg-Marquardt with a
// numerically-differentiated Jacobian -- no analytical derivatives, no
// external linear-algebra dependency (see LinearSolve.h for the small
// from-scratch dense solver backing it).
//
// Disclosed limitations (real ones, not swept under the rug): no
// redundancy/DOF/over-constraint diagnosis -- an over-constrained sketch
// converges to a least-squares compromise (or fails to converge) rather
// than reporting exactly which constraints conflict, and an
// under-constrained sketch just settles on *some* solution near its
// starting point rather than reporting which DOFs are still free, the
// way FreeCAD's own solver (or a commercial one) would.
SolveResult solveSketch(Sketch& sketch, int maxIterations = 100, double tolerance = 1e-9);

} // namespace lcad
