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
 * scans passed to apply(). State is reset each time apply() is called.
 * 
 * IMPROVED LOGIC:
 * - If a measurement is present (>0), the state is updated using the standard Kalman gain.
 * - If a measurement is missing (0), the filter performs a prediction step (adds process noise)
 *   and propagates the previous estimated state to fill the current hole. This provides
 *   temporal interpolation for missing data.
 * 
 * The model is a random walk (constant position) with measurement noise specified by the constructor.
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
     * encounters. Subsequent frames update via scalar Kalman gain. 
     * Missing frames (zeros) are filled with the predicted state if the filter is initialized.
     * Negative results after filtering are clamped to zero.
     */
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double process_noise_mm2_;     ///< Added to covariance each frame (process noise Q).
    double measurement_noise_mm2_; ///< Observation noise variance per pixel (R).
};

}  // namespace sensor_utils
}  // namespace ouster
