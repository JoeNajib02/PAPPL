/**
 * Local planarity-aware smoothing.
 */
#pragma once

#include <cstdint>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

/**
 * @brief Smooths ranges that deviate from a local planar neighborhood.
 *
 * Computes a neighborhood mean within a square window and pulls outliers
 * (beyond plane_threshold * base_range_mm) toward the local average. Assumes
 * zero ranges are invalid and excluded from the neighborhood statistics.
 */
class PlanaritySmoother : public RepeatabilityFilter {
   public:
    /**
     * @brief Configure the smoother.
     * @param radius Neighborhood radius (pixels) used for averaging.
     * @param base_range_mm Base range used to scale the plane_threshold.
     * @param plane_threshold Fractional deviation tolerated before smoothing.
     */
    PlanaritySmoother(int radius, uint32_t base_range_mm, double plane_threshold);
    /** @brief Apply the smoothing in-place across all scans. */
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    int radius_;              ///< Neighborhood radius (pixels) for averaging.
    uint32_t base_range_mm_;  ///< Base range used to scale the plane threshold.
    double plane_threshold_;  ///< Fractional deviation allowed before smoothing.
};

}  // namespace sensor_utils
}  // namespace ouster
