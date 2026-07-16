#include "core/sketch/LinearSolve.h"

#include <algorithm>
#include <cmath>

namespace lcad {

bool solveLinearSystem(std::vector<std::vector<double>> a, std::vector<double> b, std::vector<double>& x) {
    const std::size_t n = b.size();
    if (n == 0 || a.size() != n) return false;

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivotRow = col;
        double pivotMag = std::abs(a[col][col]);
        for (std::size_t row = col + 1; row < n; ++row) {
            if (std::abs(a[row][col]) > pivotMag) {
                pivotMag = std::abs(a[row][col]);
                pivotRow = row;
            }
        }
        if (pivotMag < 1e-14) return false; // singular

        if (pivotRow != col) {
            std::swap(a[col], a[pivotRow]);
            std::swap(b[col], b[pivotRow]);
        }

        for (std::size_t row = col + 1; row < n; ++row) {
            const double factor = a[row][col] / a[col][col];
            if (factor == 0.0) continue;
            for (std::size_t k = col; k < n; ++k) a[row][k] -= factor * a[col][k];
            b[row] -= factor * b[col];
        }
    }

    x.assign(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t j = i + 1; j < n; ++j) sum -= a[i][j] * x[j];
        x[i] = sum / a[i][i];
    }
    return true;
}

} // namespace lcad
