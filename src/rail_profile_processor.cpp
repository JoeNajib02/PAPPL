#include "rail_profile_processor.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ouster {
namespace rail {
namespace {

Eigen::Vector3d compute_axis_pca(const std::vector<Eigen::Vector3d>& pts, Eigen::Vector3d& centroid) {
    centroid = Eigen::Vector3d::Zero();
    for (const auto& p : pts) centroid += p;
    centroid /= static_cast<double>(pts.size());

    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (const auto& p : pts) {
        Eigen::Vector3d d = p - centroid;
        cov.noalias() += d * d.transpose();
    }
    cov /= static_cast<double>(pts.size());

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(cov);
    if (solver.info() != Eigen::Success) {
        throw std::runtime_error("PCA failed for rail axis estimation");
    }
    // Largest eigenvalue corresponds to rail longitudinal axis.
    int idx;
    solver.eigenvalues().maxCoeff(&idx);
    Eigen::Vector3d axis = solver.eigenvectors().col(idx).normalized();
    // Enforce a stable sign (positive dot with +X to avoid flipping).
    if (axis.x() < 0) axis = -axis;
    return axis;
}

template <typename T>
T median(std::vector<T> v) {
    if (v.empty()) return T{};
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    if (v.size() % 2 == 1) return v[mid];
    auto v1 = *std::max_element(v.begin(), v.begin() + mid);
    return (v[mid] + v1) / static_cast<T>(2);
}

std::vector<double> sliding_median(const std::vector<double>& data, int window) {
    if (window <= 1 || data.empty()) return data;
    if (window % 2 == 0) --window;  // enforce odd
    int half = window / 2;
    std::vector<double> out(data.size());
    std::vector<double> buf;
    buf.reserve(window);
    for (size_t i = 0; i < data.size(); ++i) {
        buf.clear();
        int start = static_cast<int>(std::max<size_t>(0, i >= static_cast<size_t>(half) ? i - half : 0));
        int end = static_cast<int>(std::min<size_t>(data.size() - 1, i + half));
        for (int j = start; j <= end; ++j) buf.push_back(data[j]);
        out[i] = median(buf);
    }
    return out;
}

std::vector<double> savitzky_golay_coeffs(int window, int poly_order) {
    if (window % 2 == 0 || window < 3) throw std::invalid_argument("Savitzky-Golay window must be odd and >=3");
    if (poly_order >= window) throw std::invalid_argument("Savitzky-Golay polynomial order must be < window size");
    int half = window / 2;
    Eigen::MatrixXd A(window, poly_order + 1);
    for (int i = -half; i <= half; ++i) {
        for (int j = 0; j <= poly_order; ++j) {
            A(i + half, j) = std::pow(static_cast<double>(i), j);
        }
    }
    // Compute smoothing (0th derivative) convolution coefficients: (A^T A)^{-1} A^T * e0
    Eigen::VectorXd e0 = Eigen::VectorXd::Zero(window);
    e0(half) = 1.0;
    Eigen::VectorXd coeffs = (A.transpose() * A).ldlt().solve(A.transpose() * e0);
    return std::vector<double>(coeffs.data(), coeffs.data() + coeffs.size());
}

std::vector<double> savitzky_golay_smooth(const std::vector<double>& data, int window, int poly_order) {
    if (data.empty()) return data;
    if (window % 2 == 0) --window;
    int half = window / 2;
    auto c = savitzky_golay_coeffs(window, poly_order);
    std::vector<double> out(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        double acc = 0.0;
        for (int k = -half; k <= half; ++k) {
            int idx = static_cast<int>(i) + k;
            if (idx < 0) idx = 0;
            if (idx >= static_cast<int>(data.size())) idx = static_cast<int>(data.size()) - 1;
            acc += c[static_cast<size_t>(k + half)] * data[static_cast<size_t>(idx)];
        }
        out[i] = acc;
    }
    return out;
}

}  // namespace

RailProfileProcessor::RailProfileProcessor(RailProfileConfig cfg) : cfg_(std::move(cfg)) {}

std::vector<RailProfileSample> RailProfileProcessor::process(const std::vector<Eigen::Vector3d>& points) const {
    if (points.size() < 10) {
        throw std::invalid_argument("Not enough points to estimate rail profile");
    }

    Eigen::Vector3d centroid;
    Eigen::Vector3d axis = compute_axis_pca(points, centroid);
    Eigen::Vector3d up(0.0, 0.0, 1.0);  // use gravity-aligned up for vertical deviations

    std::vector<double> s_vals(points.size());
    std::vector<double> z_vals(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        s_vals[i] = (p - centroid).dot(axis);
        z_vals[i] = p.dot(up);
    }

    // Bin along rail axis.
    double s_min = *std::min_element(s_vals.begin(), s_vals.end());
    double s_max = *std::max_element(s_vals.begin(), s_vals.end());
    size_t bin_count = static_cast<size_t>(std::max(1.0, std::ceil((s_max - s_min) / cfg_.bin_size_m)));
    std::vector<std::vector<double>> bins(bin_count);
    for (size_t i = 0; i < s_vals.size(); ++i) {
        size_t idx = static_cast<size_t>((s_vals[i] - s_min) / cfg_.bin_size_m);
        if (idx >= bin_count) idx = bin_count - 1;
        bins[idx].push_back(z_vals[i]);
    }

    std::vector<double> s_track;
    std::vector<double> z_median;
    s_track.reserve(bin_count);
    z_median.reserve(bin_count);
    for (size_t i = 0; i < bin_count; ++i) {
        if (bins[i].empty()) continue;
        double s = s_min + (static_cast<double>(i) + 0.5) * cfg_.bin_size_m;
        s_track.push_back(s);
        z_median.push_back(median(bins[i]));
    }
    if (z_median.size() < 5) {
        throw std::runtime_error("Insufficient populated bins to compute rail profile");
    }

    // Remove global slope (linear fit) to avoid absorbing local bumps.
    Eigen::MatrixXd A(z_median.size(), 2);
    Eigen::VectorXd b(z_median.size());
    for (size_t i = 0; i < z_median.size(); ++i) {
        A(static_cast<int>(i), 0) = s_track[i];
        A(static_cast<int>(i), 1) = 1.0;
        b(static_cast<int>(i)) = z_median[i];
    }
    Eigen::Vector2d coeff = (A.transpose() * A).ldlt().solve(A.transpose() * b);

    std::vector<double> residual(z_median.size());
    for (size_t i = 0; i < z_median.size(); ++i) {
        double trend = coeff[0] * s_track[i] + coeff[1];
        residual[i] = z_median[i] - trend;
    }

    auto residual_med = sliding_median(residual, cfg_.median_window);
    auto residual_sg = savitzky_golay_smooth(residual_med, cfg_.sg_window, cfg_.sg_poly);

    std::vector<RailProfileSample> out;
    out.reserve(residual_sg.size());
    for (size_t i = 0; i < residual_sg.size(); ++i) {
        double mm = residual_sg[i] * 1000.0;
        double abs_mm = std::abs(mm);
        if (abs_mm < cfg_.noise_floor_mm) {
            mm = 0.0;
        }
        out.push_back({s_track[i], mm});
    }

    return out;
}

}  // namespace rail
}  // namespace ouster
