/**
 * Per-pixel 1D Kalman filter over range measurements.
 */

#include "repeatability/kalman_range_filter.h"

#include <vector>

#include "repeatability/filter_utils.h"

namespace ouster {
namespace sensor_utils {

KalmanRangeFilter::KalmanRangeFilter(double process_noise_mm2,
                                     double measurement_noise_mm2)
    : process_noise_mm2_(process_noise_mm2),
      measurement_noise_mm2_(measurement_noise_mm2) {}

void KalmanRangeFilter::apply(std::vector<ouster::LidarScan>& scans,
                              const sensor::sensor_info& info) {
    if (scans.empty()) return;
    const size_t h = static_cast<size_t>(scans.front().h);
    const size_t w = static_cast<size_t>(scans.front().w);
    const size_t total = h * w;
    (void)info;

    std::vector<double> state(total, 0.0);
    std::vector<double> cov(total, 0.0);
    std::vector<bool> initialized(total, false);

    for (auto& scan : scans) {
        auto range = scan.field<uint32_t>(sensor::ChanField::RANGE);
        for (size_t r = 0; r < h; ++r) {
            for (size_t c = 0; c < w; ++c) {
                const size_t idx = r * w + c;
                const uint32_t meas =
                    range(static_cast<int>(r), static_cast<int>(c));
                if (meas == 0) continue;

                if (!initialized[idx]) {
                    state[idx] = static_cast<double>(meas);
                    cov[idx] = measurement_noise_mm2_;
                    initialized[idx] = true;
                } else {
                    cov[idx] += process_noise_mm2_;
                    const double k_gain =
                        cov[idx] / (cov[idx] + measurement_noise_mm2_);
                    state[idx] =
                        state[idx] + k_gain * (static_cast<double>(meas) - state[idx]);
                    cov[idx] = (1.0 - k_gain) * cov[idx];
                }
                range(static_cast<int>(r), static_cast<int>(c)) =
                    static_cast<uint32_t>(clamp_non_negative(state[idx]));
            }
        }
    }
}

}  // namespace sensor_utils
}  // namespace ouster

