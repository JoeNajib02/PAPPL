/**
 * Per-pixel 1D Kalman filter over range measurements.
 */
#pragma once

#include <vector>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

/**
 * @brief Applies a simple independent 1D Kalman filter per pixel.
 *
 * State (range + covariance) is maintained per pixel across the sequence of
 * scans passed to apply(). State is reset each time apply() is called. Zero
 * measurements are treated as missing data (no update). The model is a random
 * walk with measurement noise specified by the constructor.
 */
class KalmanRangeFilter : public RepeatabilityFilter {
   public:
    /**
     * @brief Configure the filter with process and measurement noise (mm^2).
     * @param process_noise_mm2 Process noise variance applied each frame.
     * @param measurement_noise_mm2 Measurement noise variance per observation.
     */
    KalmanRangeFilter(double process_noise_mm2, double measurement_noise_mm2);
    /**
     * @brief Smooth ranges in-place across all scans (per-pixel time series).
     *
     * Initializes each pixel's state on the first non-zero measurement it
     * encounters. Subsequent frames update via scalar Kalman gain. Negative
     * results after filtering are clamped to zero.
     */
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double process_noise_mm2_;     ///< Added to covariance each frame (process noise Q).
    double measurement_noise_mm2_; ///< Observation noise variance per pixel (R).
};

}  // namespace sensor_utils
}  // namespace ouster
