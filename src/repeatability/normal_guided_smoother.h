/**
 * Geometry-guided smoothing using local normals from XYZ.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "repeatability/filter_base.h"

namespace ouster {
namespace sensor_utils {

/**
 * @brief Blends neighboring ranges whose surface normals align.
 *
 * Computes local normals from cartesian coordinates, then blends each range
 * with its 4-neighbors when the angular difference is within max_angle_deg.
 * Zero ranges are treated as missing and skipped in both normal estimation and
 * blending. Normal computation uses forward differences; edges without full
 * neighbors are left untouched.
 */
class NormalGuidedSmoother : public RepeatabilityFilter {
   public:
    /**
     * @brief Construct a smoother guided by angular difference between normals.
     * @param max_angle_deg Maximum allowed normal difference in degrees.
     * @param blend Blend factor between neighbor mean and original range [0,1].
     */
    NormalGuidedSmoother(double max_angle_deg, double blend);
    /** @brief Apply the normal-guided smoothing in-place. */
    void apply(std::vector<ouster::LidarScan>& scans,
               const sensor::sensor_info& info) override;

   private:
    double max_angle_rad_; ///< Maximum allowed angular difference (radians).
    double blend_;         ///< Blend factor applied when smoothing [0,1].
};

}  // namespace sensor_utils
}  // namespace ouster
