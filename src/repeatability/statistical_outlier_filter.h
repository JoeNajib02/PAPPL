/**
 * Statistical outlier rejection filter.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

/**
 * @brief Rejects ranges that violate a global z-score threshold.
 *
 * Computes mean/stddev over all valid ranges per scan, zeroing points that are
 * either below min_range_mm or beyond z_thresh * stddev from the mean. The
 * threshold is recomputed for each scan independently; no temporal smoothing.
 */
class StatisticalOutlierFilter : public RepeatabilityFilter {
   public:
    /**
     * @brief Configure the outlier filter.
     * @param z_thresh Z-score multiplier used to reject outliers.
     * @param min_range_mm Minimum valid range; smaller values are zeroed.
     */
    StatisticalOutlierFilter(double z_thresh, uint32_t min_range_mm);
    /** @brief Apply outlier rejection in-place across all scans. */
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double z_thresh_;        ///< Z-score multiplier for outlier rejection.
    uint32_t min_range_mm_;  ///< Minimum acceptable range before zeroing.
};

}  // namespace sensor_utils
}  // namespace ouster
