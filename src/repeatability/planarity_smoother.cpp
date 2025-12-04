/**
 * Local planarity-aware smoothing implementation.
 */

#include "repeatability/planarity_smoother.h"

#include <Eigen/Dense>
#include <cmath>

#include "repeatability/filter_utils.h"

namespace ouster {
namespace sensor_utils {

PlanaritySmoother::PlanaritySmoother(int radius,
                                     uint32_t base_range_mm,
                                     double plane_threshold)
    : radius_(radius),
      base_range_mm_(base_range_mm),
      plane_threshold_(plane_threshold) {}

void PlanaritySmoother::apply(std::vector<ouster::LidarScan>& scans,
                              const sensor::sensor_info& info) {
    (void)info;
    const double threshold_mm = plane_threshold_ * static_cast<double>(base_range_mm_);
    for (auto& scan : scans) {
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        Eigen::Matrix<uint32_t, Eigen::Dynamic, Eigen::Dynamic> original = range;
        for (int r = 0; r < range.rows(); ++r) {
            for (int c = 0; c < range.cols(); ++c) {
                const uint32_t center = original(r, c);
                if (center == 0) continue;

                double sum = 0.0;
                size_t count = 0;
                for (int dr = -radius_; dr <= radius_; ++dr) {
                    const int rr = r + dr;
                    if (rr < 0 || rr >= range.rows()) continue;
                    for (int dc = -radius_; dc <= radius_; ++dc) {
                        const int cc = c + dc;
                        if (cc < 0 || cc >= range.cols()) continue;
                        const uint32_t v = original(rr, cc);
                        if (v == 0) continue;
                        sum += static_cast<double>(v);
                        count++;
                    }
                }
                if (count == 0) continue;
                const double avg = sum / static_cast<double>(count);
                if (std::abs(static_cast<double>(center) - avg) > threshold_mm) {
                    range(r, c) = static_cast<uint32_t>(clamp_non_negative(avg));
                }
            }
        }
    }
}

}  // namespace sensor_utils
}  // namespace ouster

