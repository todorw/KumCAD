#pragma once

#include <vector>

namespace lcad {

// Solves the dense n x n system a*x = b via Gaussian elimination with
// partial pivoting. Returns false (leaving x untouched) if a is singular
// to numerical precision. A small from-scratch solver -- no external
// linear-algebra library (Eigen etc.) is wired into this codebase, and a
// sketch's variable count is small enough (tens, not thousands) that a
// plain dense O(n^3) elimination is plenty fast.
bool solveLinearSystem(std::vector<std::vector<double>> a, std::vector<double> b, std::vector<double>& x);

} // namespace lcad
