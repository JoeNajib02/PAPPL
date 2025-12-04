/**
 * Geometry-guided smoothing using local normals from XYZ.
 *
 * For each point, computes a simple normal from right/down neighbors and
 * blends the range with neighbors whose normals are within an angular
 * threshold.
 */

#include "repeatability/normal_guided_smoother.h"

#include <cmath>
#include <vector>

#include <Eigen/Dense>

#include "ouster/cartesian.h"
#include "repeatability/filter_utils.h"

namespace ouster {
namespace sensor_utils {

NormalGuidedSmoother::NormalGuidedSmoother(double max_angle_deg, double blend)
    : max_angle_rad_(max_angle_deg * M_PI / 180.0), blend_(blend) {}

void NormalGuidedSmoother::apply(std::vector<ouster::LidarScan>& scans,
                                 const sensor::sensor_info& info) {
    if (scans.empty()) return;
    const double cos_thresh = std::cos(max_angle_rad_);

    for (auto& scan : scans) {
        const auto lut = ouster::make_xyz_lut(info, true);
        auto points = ouster::cartesian(scan, lut);  // rows = h*w, cols=3
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        const size_t h = static_cast<size_t>(scan.h);
        const size_t w = static_cast<size_t>(scan.w);

        // Precompute normals (forward differences)
        std::vector<Eigen::Vector3d> normals(points.rows(),
                                             Eigen::Vector3d::Zero());
        for (size_t r = 0; r < h; ++r) {
            for (size_t c = 0; c < w; ++c) {
                const size_t idx = r * w + c;
                if (range(static_cast<int>(r), static_cast<int>(c)) == 0) continue;
                if (c + 1 >= w || r + 1 >= h) continue;
                const size_t idx_right = r * w + (c + 1);
                const size_t idx_down = (r + 1) * w + c;
                if (range(static_cast<int>(r), static_cast<int>(c + 1)) == 0 ||
                    range(static_cast<int>(r + 1), static_cast<int>(c)) == 0) {
                    continue;
                }

                Eigen::Vector3d p = points.row(static_cast<int>(idx));
                Eigen::Vector3d pr = points.row(static_cast<int>(idx_right));
                Eigen::Vector3d pd = points.row(static_cast<int>(idx_down));
                Eigen::Vector3d v1 = pr - p;
                Eigen::Vector3d v2 = pd - p;
                Eigen::Vector3d n = v1.cross(v2);
                double n_norm = n.norm();
                if (n_norm > 1e-6) {
                    normals[idx] = n / n_norm;
                }
            }
        }

        auto accumulate_if_similar = [&](size_t r, size_t c,
                                         const Eigen::Vector3d& n0,
                                         double& accum, double& weight) {
            const size_t idx = r * w + c;
            const uint32_t nr = range(static_cast<int>(r), static_cast<int>(c));
            if (nr == 0) return;
            const Eigen::Vector3d& nn = normals[idx];
            if (nn.isZero(1e-12)) return;
            const double cosang = n0.dot(nn);
            if (cosang >= cos_thresh) {
                accum += static_cast<double>(nr) * cosang;
                weight += cosang;
            }
        };

        for (size_t r = 0; r < h; ++r) {
            for (size_t c = 0; c < w; ++c) {
                const size_t idx = r * w + c;
                const uint32_t center_range =
                    range(static_cast<int>(r), static_cast<int>(c));
                if (center_range == 0) continue;
                const Eigen::Vector3d& n0 = normals[idx];
                if (n0.isZero(1e-12)) continue;

                double accum = 0.0;
                double weight = 0.0;

                if (r > 0) accumulate_if_similar(r - 1, c, n0, accum, weight);
                if (r + 1 < h) accumulate_if_similar(r + 1, c, n0, accum, weight);
                if (c > 0) accumulate_if_similar(r, c - 1, n0, accum, weight);
                if (c + 1 < w) accumulate_if_similar(r, c + 1, n0, accum, weight);

                if (weight > 0.0) {
                    const double neighbor_mean = accum / weight;
                    const double blended =
                        blend_ * neighbor_mean + (1.0 - blend_) * center_range;
                    range(static_cast<int>(r), static_cast<int>(c)) =
                        static_cast<uint32_t>(clamp_non_negative(blended));
                }
            }
        }
    }
}

}  // namespace sensor_utils
}  // namespace ouster

