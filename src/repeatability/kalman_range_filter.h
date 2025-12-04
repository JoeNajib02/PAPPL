/**
 * Per-pixel 1D Kalman filter over range measurements.
 */
#pragma once

#include <vector>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

class KalmanRangeFilter : public RepeatabilityFilter {
   public:
    KalmanRangeFilter(double process_noise_mm2, double measurement_noise_mm2);
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double process_noise_mm2_;
    double measurement_noise_mm2_;
};

}  // namespace sensor_utils
}  // namespace ouster

