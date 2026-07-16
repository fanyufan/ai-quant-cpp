// common/svd.hpp
// 轻量 header-only Jacobi SVD 实现，仅依赖标准库，避免引入 Eigen。
// 适用于中小规模稠密矩阵（如收益率矩阵、特征矩阵）。

#ifndef QUANT_SVD_HPP
#define QUANT_SVD_HPP

#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

namespace quant {

namespace detail {

inline double hypot(double a, double b) {
    return std::sqrt(a * a + b * b);
}

// Jacobi eigenvalue decomposition of a symmetric matrix M (n x n).
// Returns eigenvalues in ascending order and corresponding eigenvectors as columns of Q.
inline bool jacobi_eig(const std::vector<std::vector<double>>& M,
                       std::vector<double>& eigvals,
                       std::vector<std::vector<double>>& eigvecs,
                       int max_sweeps = 100,
                       double tol = 1e-12) {
    const size_t n = M.size();
    if (n == 0) return true;
    for (const auto& row : M) if (row.size() != n) return false;

    eigvals.assign(n, 0.0);
    eigvecs.assign(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) eigvecs[i][i] = 1.0;

    std::vector<std::vector<double>> A = M;

    for (int sweep = 0; sweep < max_sweeps; ++sweep) {
        double off_norm = 0.0;
        for (size_t p = 0; p < n; ++p) {
            for (size_t q = p + 1; q < n; ++q) {
                off_norm += std::abs(A[p][q]);
            }
        }
        if (off_norm <= tol) break;

        for (size_t p = 0; p < n; ++p) {
            for (size_t q = p + 1; q < n; ++q) {
                double app = A[p][p];
                double aqq = A[q][q];
                double apq = A[p][q];
                if (std::abs(apq) < tol) continue;
                double phi = 0.5 * std::atan2(2.0 * apq, aqq - app);
                double c = std::cos(phi);
                double s = std::sin(phi);

                for (size_t i = 0; i < n; ++i) {
                    double aip = A[i][p];
                    double aiq = A[i][q];
                    A[i][p] = c * aip - s * aiq;
                    A[i][q] = s * aip + c * aiq;
                }
                for (size_t i = 0; i < n; ++i) {
                    double api = A[p][i];
                    double aqi = A[q][i];
                    A[p][i] = c * api - s * aqi;
                    A[q][i] = s * api + c * aqi;
                }
                // Update eigenvectors columns p and q.
                for (size_t i = 0; i < n; ++i) {
                    double eip = eigvecs[i][p];
                    double eiq = eigvecs[i][q];
                    eigvecs[i][p] = c * eip - s * eiq;
                    eigvecs[i][q] = s * eip + c * eiq;
                }
            }
        }
    }

    for (size_t i = 0; i < n; ++i) eigvals[i] = A[i][i];

    // Sort eigenvalues descending.
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&eigvals](size_t a, size_t b) { return eigvals[a] > eigvals[b]; });

    std::vector<double> sorted_vals(n);
    std::vector<std::vector<double>> sorted_vecs(n, std::vector<double>(n));
    for (size_t i = 0; i < n; ++i) {
        sorted_vals[i] = eigvals[idx[i]];
        for (size_t j = 0; j < n; ++j) sorted_vecs[j][i] = eigvecs[j][idx[i]];
    }
    eigvals = std::move(sorted_vals);
    eigvecs = std::move(sorted_vecs);
    return true;
}

inline std::vector<std::vector<double>> matmul(const std::vector<std::vector<double>>& A,
                                               const std::vector<std::vector<double>>& B) {
    size_t m = A.size();
    size_t p = A.empty() ? 0 : A[0].size();
    size_t n = B.empty() ? 0 : B[0].size();
    std::vector<std::vector<double>> C(m, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < m; ++i) {
        for (size_t k = 0; k < p; ++k) {
            double aik = A[i][k];
            for (size_t j = 0; j < n; ++j) C[i][j] += aik * B[k][j];
        }
    }
    return C;
}

inline std::vector<std::vector<double>> transpose(const std::vector<std::vector<double>>& A) {
    if (A.empty()) return {};
    size_t m = A.size(), n = A[0].size();
    std::vector<std::vector<double>> T(n, std::vector<double>(m));
    for (size_t i = 0; i < m; ++i)
        for (size_t j = 0; j < n; ++j) T[j][i] = A[i][j];
    return T;
}

inline std::vector<std::vector<double>> make_zeros(size_t m, size_t n) {
    return std::vector<std::vector<double>>(m, std::vector<double>(n, 0.0));
}

} // namespace detail

// Compute thin SVD of A (m x n): A = U * diag(sigma) * Vt
//   U    : m x k
//   sigma: k
//   Vt   : k x n
// where k = min(m, n).
// Singular values are sorted in descending order.
inline bool svd(const std::vector<std::vector<double>>& A,
                std::vector<std::vector<double>>& U,
                std::vector<double>& sigma,
                std::vector<std::vector<double>>& Vt) {
    const size_t m = A.size();
    if (m == 0) { U.clear(); sigma.clear(); Vt.clear(); return true; }
    const size_t n = A[0].size();
    if (n == 0) { U.clear(); sigma.clear(); Vt.clear(); return true; }

    using detail::jacobi_eig;
    using detail::matmul;
    using detail::transpose;
    using detail::make_zeros;

    const double eps = std::numeric_limits<double>::epsilon();

    if (m >= n) {
        // Compute A^T * A -> V contains right singular vectors.
        std::vector<std::vector<double>> AtA(n, std::vector<double>(n, 0.0));
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i; j < n; ++j) {
                double s = 0.0;
                for (size_t r = 0; r < m; ++r) s += A[r][i] * A[r][j];
                AtA[i][j] = AtA[j][i] = s;
            }
        }
        std::vector<double> eigvals;
        std::vector<std::vector<double>> V;
        if (!jacobi_eig(AtA, eigvals, V)) return false;

        sigma.resize(n);
        for (size_t i = 0; i < n; ++i) {
            sigma[i] = (eigvals[i] > 0.0) ? std::sqrt(eigvals[i]) : 0.0;
        }

        // U = A * V * diag(1/sigma)
        auto AV = matmul(A, V); // m x n
        U.assign(m, std::vector<double>(n));
        for (size_t i = 0; i < m; ++i) {
            for (size_t j = 0; j < n; ++j) {
                U[i][j] = (sigma[j] > eps) ? AV[i][j] / sigma[j] : 0.0;
            }
        }
        // Vt = V^T
        Vt = transpose(V); // n x n
    } else {
        // Compute A * A^T -> U contains left singular vectors.
        std::vector<std::vector<double>> AAt(m, std::vector<double>(m, 0.0));
        for (size_t i = 0; i < m; ++i) {
            for (size_t j = i; j < m; ++j) {
                double s = 0.0;
                for (size_t c = 0; c < n; ++c) s += A[i][c] * A[j][c];
                AAt[i][j] = AAt[j][i] = s;
            }
        }
        std::vector<double> eigvals;
        if (!jacobi_eig(AAt, eigvals, U)) return false;

        sigma.resize(m);
        for (size_t i = 0; i < m; ++i) {
            sigma[i] = (eigvals[i] > 0.0) ? std::sqrt(eigvals[i]) : 0.0;
        }

        // Vt = diag(1/sigma) * U^T * A
        auto UtA = matmul(transpose(U), A); // m x n
        Vt.assign(m, std::vector<double>(n));
        for (size_t i = 0; i < m; ++i) {
            for (size_t j = 0; j < n; ++j) {
                Vt[i][j] = (sigma[i] > eps) ? UtA[i][j] / sigma[i] : 0.0;
            }
        }
    }
    return true;
}

} // namespace quant

#endif // QUANT_SVD_HPP
