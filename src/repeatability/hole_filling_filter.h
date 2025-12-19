/**
 * @file hole_filling_filter.h
 * @brief Spatial hole filling filter to interpolate missing data.
 */
#pragma once

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

/**
 * @brief Fills invalid (zero-range) pixels by interpolating from valid neighbors.
 *
 * This filter iterates over the range image. For every pixel that has a range of 0
 * (invalid/missing return), it examines a local neighborhood. If enough valid neighbors
 * are found, the pixel is replaced by the mean (or median) of those neighbors.
 * This helps reduce the number of NaNs in the final output and provides a more continuous
 * surface for subsequent analysis.
 */
class HoleFillingFilter : public RepeatabilityFilter {
   public:
    /**
     * @brief Construct a new Hole Filling Filter.
     *
     * @param kernel_size The size of the sliding window (e.g., 3 means 3x3). Must be odd.
     * @param min_valid_neighbors Minimum number of valid neighbors required to fill a hole.
     */
    HoleFillingFilter(int kernel_size = 3, int min_valid_neighbors = 3);

    /**
     * @brief Apply the hole filling filter to a set of scans.
     *
     * @param scans The scans to process in-place.
     * @param info Sensor metadata.
     */
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    int kernel_size_;
    int min_valid_neighbors_;
};

}  // namespace sensor_utils
}  // namespace ouster

